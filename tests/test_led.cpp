// LED Display Test
// Cycles through several visual patterns to verify the LED panels work.
// Run with:  sudo ./build/test_led [config_path]
//
// Patterns:
//   1. Solid red, green, blue (1s each)
//   2. Horizontal gradient (red->blue)
//   3. Vertical gradient (green->red)
//   4. Checkerboard pattern
//   5. Single-pixel chase
//   6. All off

#include "Config.h"
#include <led-matrix.h>
#include <signal.h>
#include <unistd.h>
#include <iostream>
#include <cmath>
#include <chrono>
#include <thread>

static volatile bool g_running = true;
static void sigHandler(int) { g_running = false; }

static void waitOrAbort(int ms) {
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (g_running && std::chrono::steady_clock::now() < end)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

int main(int argc, char* argv[]) {
    signal(SIGINT, sigHandler);
    signal(SIGTERM, sigHandler);

    Config cfg;
    if (argc > 1)
        cfg.loadFromFile(argv[1]);
    else
        cfg.loadFromFile("config/default.cfg");

    rgb_matrix::RGBMatrix::Options opts;
    opts.rows             = cfg.led_rows;
    opts.cols             = cfg.led_cols;
    opts.parallel         = cfg.led_parallel;
    opts.chain_length     = cfg.led_chain_length;
    opts.brightness       = cfg.led_brightness;
    opts.hardware_mapping = cfg.led_hardware_mapping.c_str();

    rgb_matrix::RuntimeOptions rtOpts;
    rtOpts.gpio_slowdown = cfg.led_gpio_slowdown;

    rgb_matrix::RGBMatrix* matrix =
        rgb_matrix::RGBMatrix::CreateFromOptions(opts, rtOpts);
    if (!matrix) {
        std::cerr << "Failed to create RGBMatrix. Are you running as root?\n";
        return 1;
    }

    auto* canvas = matrix->CreateFrameCanvas();
    const int W = cfg.led_cols;
    const int H = cfg.led_rows * cfg.led_parallel;

    std::cout << "LED Test: display is " << W << "x" << H << "\n";

    // --- Test 1: Solid colors ---
    struct { uint8_t r, g, b; const char* name; } solids[] = {
        {255,   0,   0, "RED"},
        {  0, 255,   0, "GREEN"},
        {  0,   0, 255, "BLUE"},
        {255, 255, 255, "WHITE"},
    };
    for (auto& c : solids) {
        if (!g_running) break;
        std::cout << "  Solid " << c.name << "\n";
        canvas->Fill(c.b, c.r, c.g);
        canvas = matrix->SwapOnVSync(canvas);
        waitOrAbort(1500);
    }

    // --- Test 2: Horizontal gradient (red -> blue) ---
    if (g_running) {
        std::cout << "  Horizontal gradient\n";
        canvas->Clear();
        for (int x = 0; x < W; ++x) {
            uint8_t r = static_cast<uint8_t>(255 * (W - 1 - x) / (W - 1));
            uint8_t b = static_cast<uint8_t>(255 * x / (W - 1));
            for (int y = 0; y < H; ++y)
                canvas->SetPixel(x, y, b, r, 0);
        }
        canvas = matrix->SwapOnVSync(canvas);
        waitOrAbort(2000);
    }

    // --- Test 3: Vertical gradient (green -> red) ---
    if (g_running) {
        std::cout << "  Vertical gradient\n";
        canvas->Clear();
        for (int y = 0; y < H; ++y) {
            uint8_t g = static_cast<uint8_t>(255 * (H - 1 - y) / (H - 1));
            uint8_t r = static_cast<uint8_t>(255 * y / (H - 1));
            for (int x = 0; x < W; ++x)
                canvas->SetPixel(x, y, 0, r, g);
        }
        canvas = matrix->SwapOnVSync(canvas);
        waitOrAbort(2000);
    }

    // --- Test 4: Checkerboard ---
    if (g_running) {
        std::cout << "  Checkerboard (8px)\n";
        canvas->Clear();
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                bool on = ((x / 8) + (y / 8)) % 2 == 0;
                canvas->SetPixel(x, y, on ? 255 : 0, on ? 255 : 0, on ? 255 : 0);
            }
        canvas = matrix->SwapOnVSync(canvas);
        waitOrAbort(2000);
    }

    // --- Test 5: Pixel chase ---
    if (g_running) {
        std::cout << "  Pixel chase\n";
        for (int i = 0; i < W * H && g_running; i += 4) {
            int x = i % W;
            int y = i / W;
            canvas->Clear();
            canvas->SetPixel(x, y, 0, 255, 255);
            canvas = matrix->SwapOnVSync(canvas);
            waitOrAbort(10);
        }
    }

    // --- Cleanup ---
    std::cout << "  All off\n";
    canvas->Clear();
    matrix->SwapOnVSync(canvas);
    waitOrAbort(500);

    delete matrix;
    std::cout << "LED test complete.\n";
    return 0;
}
