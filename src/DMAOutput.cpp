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

    const FrameSet* frame = nullptr;
    // Persist across loop iterations for per-slice continuity/telemetry.
    int64_t lastSlice = -1;
    int64_t lastUpdateUs = 0;

    while (running_) {
        // Poll for newly committed network frames and swap immediately.
        const FrameSet* latest = fb_.tryAcquireRead();
        if (latest) {
            frame = latest;
            lastSlice = -1; // force refresh even if current slice index is unchanged
            lastUpdateUs = 0;
            if (logger_)
                logger_->log("led_frame_swap", 1);
        }

        if (!frame) {
            std::this_thread::sleep_for(std::chrono::microseconds(200));
            continue;
        }

        int64_t rotUs = hall_.lastRotationUs();
        if (rotUs <= 0) {
            // No rotation data yet — show slice 0 on both panels.
            updatePanels(frame, 0, 0);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        int64_t nowUs = monoUs();
        int64_t edgeUs = lastEdgeUs_.load(std::memory_order_relaxed);

        // Compute current slice index from elapsed time since last edge.
        int64_t elapsed = (edgeUs > 0) ? (nowUs - edgeUs) : 0;
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
