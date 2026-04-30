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
    thread_     = std::thread(&DMAOutput::run, this);
    rateThread_ = std::thread(&DMAOutput::chainRateLoop, this);
    std::cout << "DMAOutput: started\n";
    return true;
}

void DMAOutput::stop() {
    running_ = false;
    if (thread_.joinable())     thread_.join();
    if (rateThread_.joinable()) rateThread_.join();
    hub75_dma_shutdown(&dma_);
}

// Periodically samples the DMA-written chain_timer in a tight loop for ~50 ms,
// counts distinct values, and prints the resulting refresh rate. Runs at
// default priority so it doesn't perturb the SCHED_FIFO panel-update thread.
void DMAOutput::chainRateLoop() {
    using namespace std::chrono;
    while (running_) {
        std::this_thread::sleep_for(seconds(2));
        if (!running_) break;

        const auto t0 = steady_clock::now();
        uint32_t prev  = hub75_dma_chain_timer(&dma_);
        uint32_t first = prev;
        int      count = 0;
        const auto deadline = t0 + milliseconds(50);
        while (steady_clock::now() < deadline) {
            uint32_t cur = hub75_dma_chain_timer(&dma_);
            if (cur != prev) { ++count; prev = cur; }
        }
        const auto t1 = steady_clock::now();
        int64_t   wallUs = duration_cast<microseconds>(t1 - t0).count();
        // STC_LOW is the 1 MHz timer; (prev - first) directly equals the
        // µs span between the first and last observed chain completions.
        int64_t   stcUs  = (int64_t)(uint32_t)(prev - first);

        if (count > 0 && wallUs > 0) {
            double rateHz       = count * 1e6 / (double)wallUs;
            double intervalUs   = (double)wallUs / count;
            double avgChainUs   = count > 1 ? (double)stcUs / (count - 1) : 0.0;
            std::cerr << "[chain] " << count << " refreshes in " << wallUs
                      << " µs → " << intervalUs << " µs/refresh ("
                      << rateHz << " Hz), STC delta " << avgChainUs << " µs\n";
            if (logger_) {
                logger_->log("chain_refresh_us", (int64_t)intervalUs);
            }
        } else {
            std::cerr << "[chain] no refreshes seen in " << wallUs << " µs\n";
        }
    }
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
