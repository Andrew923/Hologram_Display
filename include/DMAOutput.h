#pragma once

#include "FrameBuffer.h"
#include "HallSensor.h"
#include "Config.h"
#include "hub75_dma.h"

#include <atomic>
#include <thread>
#include <chrono>

// Drop-in replacement for LEDOutput using direct DMA-driven HUB75 scanning.
// The DMA engine continuously refreshes the LED panels from a pixel buffer;
// the run() thread updates that buffer as the display rotates.
class DMAOutput {
public:
    DMAOutput(FrameBuffer& fb, HallSensor& hall, const Config& cfg);
    ~DMAOutput();

    // Start DMA engine and launch the run thread. Returns false on error.
    bool start();

    // Stop DMA, join thread, release resources.
    void stop();

private:
    void run();

    void updatePanels(const FrameSet* frame, int slice0, int slice1);

    FrameBuffer&      fb_;
    HallSensor&       hall_;
    const Config&     cfg_;
    hub75_dma_state_t dma_{};
    std::atomic<bool> running_{false};
    std::thread       thread_;

    // Monotonic microsecond timestamp of the last hall-sensor edge,
    // set via HallSensor callback in start().
    std::atomic<int64_t> lastEdgeUs_{0};

    static int64_t monoUs() {
        using namespace std::chrono;
        return duration_cast<microseconds>(
            steady_clock::now().time_since_epoch()).count();
    }
};
