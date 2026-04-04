// LED Rate Test
// Measures achievable frame rate using the same rendering path as hologram_display.
// Each frame: builds a Color buffer, calls SetPixels (x2 for parallel panels), SwapOnVSync.
// Pattern: single pixel chase at 1px/frame — useful for slow-motion video analysis.
// Run with: sudo ./build/test_led_rate [config_path]

#include "Config.h"
#include <led-matrix.h>
#include <signal.h>
#include <iostream>
#include <vector>
#include <numeric>
#include <algorithm>
#include <chrono>

static volatile bool g_running = true;
static void sigHandler(int) { g_running = false; }

int main(int argc, char* argv[]) {
    signal(SIGINT, sigHandler);
    signal(SIGTERM, sigHandler);

    Config cfg;
    cfg.loadFromFile(argc > 1 ? argv[1] : "config/default.cfg");

    rgb_matrix::RGBMatrix::Options opts;
    opts.rows             = cfg.led_rows;
    opts.cols             = cfg.led_cols;
    opts.parallel         = cfg.led_parallel;
    opts.chain_length     = cfg.led_chain_length;
    opts.brightness       = cfg.led_brightness;
    opts.hardware_mapping = cfg.led_hardware_mapping.c_str();

    rgb_matrix::RuntimeOptions rtOpts;
    rtOpts.gpio_slowdown   = cfg.led_gpio_slowdown;
    rtOpts.drop_privileges = 0;

    rgb_matrix::RGBMatrix* matrix =
        rgb_matrix::RGBMatrix::CreateFromOptions(opts, rtOpts);
    if (!matrix) {
        std::cerr << "Failed to create RGBMatrix. Run as root?\n";
        return 1;
    }

    auto* canvas = matrix->CreateFrameCanvas();

    const int W      = cfg.led_cols;
    const int panelH = cfg.led_rows;           // height of one panel
    const int panels = cfg.led_parallel;
    const int N      = W * panelH;             // pixels per panel (one SetPixels call)

    std::vector<rgb_matrix::Color> buf(N);

    std::cout << "Rate test: " << W << "x" << (panelH * panels)
              << " (" << panels << " panel(s)), running 5s — Ctrl+C to stop early\n";

    using Clock = std::chrono::steady_clock;
    const auto testEnd = Clock::now() + std::chrono::seconds(5);

    std::vector<double> frameTimes;
    frameTimes.reserve(50000);

    // Chase state: one pixel lit per panel, offset by half the panel for panel 1
    int pos = 0;
    auto lastSwap = Clock::now();

    while (g_running && Clock::now() < testEnd) {
        // --- mirror of renderSlice: build buffer, SetPixels per panel ---
        for (int panel = 0; panel < panels; ++panel) {
            // Chase pixel for this panel is offset by panel*(N/2) so they move together
            int panelPos = (pos + panel * (N / 2)) % N;

            for (int i = 0; i < N; ++i)
                buf[i] = rgb_matrix::Color(0, 0, 0);

            // Yellow pixel (b,r,g order to match channel swap in LEDOutput)
            buf[panelPos] = rgb_matrix::Color(0, 255, 255);

            canvas->SetPixels(0, panel * panelH, W, panelH, buf.data());
        }

        canvas = matrix->SwapOnVSync(canvas);

        auto now = Clock::now();
        frameTimes.push_back(
            std::chrono::duration<double, std::micro>(now - lastSwap).count());
        lastSwap = now;

        pos = (pos + 1) % N;
    }

    canvas->Clear();
    matrix->SwapOnVSync(canvas);
    delete matrix;

    // Drop first frame (includes startup jitter)
    if (!frameTimes.empty()) frameTimes.erase(frameTimes.begin());

    if (frameTimes.size() < 2) {
        std::cout << "Not enough frames collected.\n";
        return 0;
    }

    std::vector<double> sorted = frameTimes;
    std::sort(sorted.begin(), sorted.end());

    double sum = std::accumulate(frameTimes.begin(), frameTimes.end(), 0.0);
    double avg = sum / frameTimes.size();
    double p50 = sorted[sorted.size() * 50 / 100];
    double p95 = sorted[sorted.size() * 95 / 100];
    double p99 = sorted[sorted.size() * 99 / 100];

    std::cout << "\n--- Results (" << frameTimes.size() << " frames) ---\n"
              << "  avg: " << avg   << " us  (" << 1e6/avg   << " fps)\n"
              << "  min: " << sorted.front() << " us  max: " << sorted.back() << " us\n"
              << "  p50: " << p50   << " us  p95: " << p95   << " us  p99: " << p99 << " us\n";

    return 0;
}
