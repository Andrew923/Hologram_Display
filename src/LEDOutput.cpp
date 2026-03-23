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
    rtOpts.gpio_slowdown = cfg_.led_gpio_slowdown;

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

    // Panel 0 (parallel chain 0): shows the slice as-is.
    // Panel 1 (parallel chain 1): shows the opposite slice (180 degrees away).
    int oppositeId = (sliceId + SLICE_COUNT / 2) % SLICE_COUNT;
    const auto& opposite = frame.slices[oppositeId];

    // Build contiguous Color buffer for panel 0 and write the whole region in one call.
    for (int i = 0; i < SLICE_W * SLICE_H; ++i)
        pixelBuf_[i] = rgb_matrix::Color(slice[i].r, slice[i].g, slice[i].b);
    canvas->SetPixels(0, 0, SLICE_W, SLICE_H, pixelBuf_.data());

    // Build contiguous Color buffer for panel 1 and write the whole region in one call.
    for (int i = 0; i < SLICE_W * SLICE_H; ++i)
        pixelBuf_[i] = rgb_matrix::Color(opposite[i].r, opposite[i].g, opposite[i].b);
    canvas->SetPixels(0, SLICE_H, SLICE_W, SLICE_H, pixelBuf_.data());
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
