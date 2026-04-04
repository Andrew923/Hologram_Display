#include "LEDOutput.h"
#include <led-matrix.h>
#include <iostream>
#include <chrono>
#include <cmath>

LEDOutput::LEDOutput(FrameBuffer& fb, HallSensor& hall, const Config& cfg)
    : fb_(fb), hall_(hall), cfg_(cfg) {}

LEDOutput::~LEDOutput() {
    stop();
}

bool LEDOutput::start() {
    // Configure the matrix
    rgb_matrix::RGBMatrix::Options opts;
    opts.rows             = cfg_.led_rows;
    opts.cols             = cfg_.led_cols;
    opts.parallel         = cfg_.led_parallel;
    opts.chain_length     = cfg_.led_chain_length;
    opts.brightness       = cfg_.led_brightness;
    opts.hardware_mapping = cfg_.led_hardware_mapping.c_str();

    rgb_matrix::RuntimeOptions rtOpts;
    rtOpts.gpio_slowdown   = cfg_.led_gpio_slowdown;
    rtOpts.drop_privileges = 0; // keep root so gpiod can open /dev/gpiochip0 after LED init

    matrix_ = rgb_matrix::RGBMatrix::CreateFromOptions(opts, rtOpts);
    if (!matrix_) {
        std::cerr << "LEDOutput: failed to create RGBMatrix\n";
        return false;
    }

    canvas_ = matrix_->CreateFrameCanvas();
    pixelBuf_.resize(SLICE_W * SLICE_H);

    running_ = true;
    thread_  = std::thread(&LEDOutput::run, this);

    std::cout << "LEDOutput: started\n";
    return true;
}

void LEDOutput::stop() {
    running_ = false;
    if (thread_.joinable())
        thread_.join();
    delete matrix_;
    matrix_ = nullptr;
}

void LEDOutput::renderSlice(rgb_matrix::FrameCanvas* canvas,
                            const FrameSet& frame, int sliceId) {
    const auto& slice = frame.slices[sliceId];

    int dispW = std::min(SLICE_W, cfg_.led_cols);
    int dispH = std::min(SLICE_H, cfg_.led_rows);
    int offX  = (SLICE_W - dispW) / 2;
    int offY  = (SLICE_H - dispH) / 2;

    // Build contiguous Color buffer for the visible region of panel 0.
    // Sample from the center of the slice so the rotation axis aligns
    // with the physical center of the LED panel.
    for (int y = 0; y < dispH; ++y)
        for (int x = 0; x < dispW; ++x)
            pixelBuf_[y * dispW + x] = rgb_matrix::Color(
                slice[(y + offY) * SLICE_W + (x + offX)].b,
                slice[(y + offY) * SLICE_W + (x + offX)].r,
                slice[(y + offY) * SLICE_W + (x + offX)].g);
    canvas->SetPixels(0, 0, dispW, dispH, pixelBuf_.data());

    // Panel 1 (opposite slice) only when using multiple parallel chains.
    if (cfg_.led_parallel >= 2) {
        int oppositeId = (sliceId + SLICE_COUNT / 2) % SLICE_COUNT;
        const auto& opposite = frame.slices[oppositeId];

        for (int y = 0; y < dispH; ++y)
            for (int x = 0; x < dispW; ++x)
                pixelBuf_[y * dispW + x] = rgb_matrix::Color(
                    opposite[(y + offY) * SLICE_W + (x + offX)].b,
                    opposite[(y + offY) * SLICE_W + (x + offX)].r,
                    opposite[(y + offY) * SLICE_W + (x + offX)].g);
        canvas->SetPixels(0, dispH, dispW, dispH, pixelBuf_.data());
    }
}

void LEDOutput::run() {
    using Clock = std::chrono::steady_clock;

    while (running_) {
        // Block until a full frame is available
        const FrameSet* frame = fb_.acquireRead();
        if (!frame)
            continue;

        // Wait for the hall sensor to give us a rotation period
        int64_t rotUs = hall_.lastRotationUs();
        if (rotUs <= 0) {
            // No rotation data yet — just display slice 0 as a test pattern
            canvas_->Clear();
            renderSlice(canvas_, *frame, 0);
            canvas_ = matrix_->SwapOnVSync(canvas_);
            continue;
        }

        // Time per slice in microseconds
        double sliceUs = static_cast<double>(rotUs) / SLICE_COUNT;

        auto rotStart = Clock::now();

        for (int s = 0; s < SLICE_COUNT && running_; ++s) {
            canvas_->Clear();
            renderSlice(canvas_, *frame, s);
            canvas_ = matrix_->SwapOnVSync(canvas_);

            // Spin-wait until it's time for the next slice.
            // We use spin-wait because the intervals are very short
            // (e.g., ~140 us at 1200 RPM) and sleep_for is too imprecise.
            auto targetTime = rotStart +
                std::chrono::microseconds(static_cast<int64_t>(sliceUs * (s + 1)));
            while (Clock::now() < targetTime) {
                // busy-wait
            }
        }
    }
}
