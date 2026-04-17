#include "DMAOutput.h"
#include "Protocol.h"

#include <iostream>
#include <algorithm>
#include <pthread.h>

DMAOutput::DMAOutput(FrameBuffer& fb, HallSensor& hall, const Config& cfg,
                     TimingLogger* logger)
    : fb_(fb), hall_(hall), cfg_(cfg), logger_(logger) {}

DMAOutput::~DMAOutput() {
    stop();
}

bool DMAOutput::start() {
    // Register callback to capture the monotonic timestamp of each hall edge.
    // HallSensor fires this callback from its own thread on each detected edge.
    hall_.setCallback([this](int64_t /*periodUs*/) {
        lastEdgeUs_.store(monoUs(), std::memory_order_relaxed);
    });

    if (hub75_dma_init(&dma_, 5) != 0) {
        std::cerr << "DMAOutput: hub75_dma_init failed\n";
        return false;
    }

    running_ = true;
    thread_  = std::thread(&DMAOutput::run, this);
    std::cout << "DMAOutput: started\n";
    return true;
}

void DMAOutput::stop() {
    running_ = false;
    if (thread_.joinable())
        thread_.join();
    hub75_dma_shutdown(&dma_);
}

void DMAOutput::updatePanels(const FrameSet* frame, int slice0, int slice1) {
    hub75_dma_update_panels(
        &dma_,
        reinterpret_cast<const hub75_rgb_t*>(frame->slices[slice0].data()),
        reinterpret_cast<const hub75_rgb_t*>(frame->slices[slice1].data()));
}

void DMAOutput::run() {
    // Elevate to real-time priority for precise rotation timing.
    struct sched_param sp{};
    sp.sched_priority = 80;
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) != 0)
        std::cerr << "DMAOutput: failed to set SCHED_FIFO (running without RT priority)\n";

    const FrameSet* lastGoodFrame = nullptr;
    int64_t repeatedRotations = 0;

    while (running_) {
        int64_t tBeforeAcquire = logger_ ? monoUs() : 0;
        if (const FrameSet* fresh = fb_.tryAcquireRead()) {
            lastGoodFrame = fresh;
            repeatedRotations = 0;
        } else if (lastGoodFrame) {
            ++repeatedRotations;
            if (logger_)
                logger_->log("led_repeated_rotation", repeatedRotations);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        if (logger_)
            logger_->log("led_acquire_read_us", monoUs() - tBeforeAcquire);

        const FrameSet* frame = lastGoodFrame;

        int64_t rotUs = hall_.lastRotationUs();
        if (rotUs <= 0) {
            // No rotation data yet — show slice 0 on both panels.
            updatePanels(frame, 0, 0);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        // Remember which hall edge was current when this frame was acquired.
        int64_t frameEdge = lastEdgeUs_.load(std::memory_order_relaxed);
        int64_t lastSlice  = -1;
        int64_t lastUpdateUs = 0;

        // Display this frame until the next hall edge arrives (= one rotation).
        // Timeout at 2× the expected rotation period to handle stopped motor.
        int64_t timeoutUs = rotUs * 2;
        int64_t frameStartUs = monoUs();

        while (running_) {
            int64_t nowUs = monoUs();

            // New rotation edge detected — move on to the next frame.
            int64_t currentEdge = lastEdgeUs_.load(std::memory_order_relaxed);
            if (currentEdge != frameEdge && currentEdge != 0)
                break;

            // Timeout guard (motor stopped or edge missed).
            if (nowUs - frameStartUs > timeoutUs)
                break;

            // Compute current slice index from elapsed time since last edge.
            int64_t elapsed = (frameEdge > 0) ? (nowUs - frameEdge) : 0;
            if (elapsed < 0) elapsed = 0;
            elapsed = elapsed % rotUs;

            int64_t slice = (elapsed * SLICE_COUNT) / rotUs;
            slice = std::clamp(slice, (int64_t)0, (int64_t)(SLICE_COUNT - 1));

            if (slice != lastSlice) {
                int opposite = static_cast<int>((slice + SLICE_COUNT / 2) % SLICE_COUNT);

                int64_t tBefore = logger_ ? monoUs() : 0;
                updatePanels(frame, static_cast<int>(slice), opposite);
                if (logger_) {
                    int64_t tAfter = monoUs();
                    logger_->log("led_update_panels_us", tAfter - tBefore);
                    if (lastSlice >= 0)
                        logger_->log("led_slice_interval_us", tBefore - lastUpdateUs);
                    lastUpdateUs = tBefore;
                }
                lastSlice = slice;
            }
            // Tight spin — DMA refreshes the panel in the background.
        }
    }
}
