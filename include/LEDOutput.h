#pragma once

#include "FrameBuffer.h"
#include "HallSensor.h"
#include "Config.h"
#include <atomic>
#include <thread>

namespace rgb_matrix {
class RGBMatrix;
class FrameCanvas;
}

class LEDOutput {
public:
    LEDOutput(FrameBuffer& fb, HallSensor& hall, const Config& cfg);
    ~LEDOutput();

    bool start();
    void stop();

private:
    void run();

    // Push one slice's pixel data to both panels on the canvas.
    void renderSlice(rgb_matrix::FrameCanvas* canvas,
                     const FrameSet& frame, int sliceId);

    FrameBuffer&  fb_;
    HallSensor&   hall_;
    const Config& cfg_;

    rgb_matrix::RGBMatrix*    matrix_  = nullptr;
    rgb_matrix::FrameCanvas*  canvas_  = nullptr;
    std::atomic<bool>         running_{false};
    std::thread               thread_;
};
