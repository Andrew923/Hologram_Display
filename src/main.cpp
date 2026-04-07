#include "Config.h"
#include "FrameBuffer.h"
#include "TimingLogger.h"
#include "UDPReceiver.h"
#include "HallSensor.h"
#include "DMAOutput.h"

#include <iostream>
#include <csignal>
#include <atomic>
#include <memory>

static std::atomic<bool> g_running{true};

static void signalHandler(int) {
    g_running = false;
}

int main(int argc, char* argv[]) {
    // --- Load configuration ---
    Config cfg;
    std::string cfgPath = "config/default.cfg";
    if (argc > 1)
        cfgPath = argv[1];
    cfg.loadFromFile(cfgPath);

    std::cout << "Hologram Display starting\n"
              << "  UDP port:      " << cfg.udp_port          << "\n"
              << "  Hall GPIO:     " << cfg.hall_gpio_pin      << "\n"
              << "  Hall bias:     " << cfg.hall_bias          << "\n"
              << "  Hall edge:     " << cfg.hall_edge          << "\n"
              << "  LED panels:    " << cfg.led_parallel
              << " x " << cfg.led_cols << "x" << cfg.led_rows << "\n"
              << "  Slice count:   " << cfg.slice_count        << "\n";

    // --- Set up signal handling for clean shutdown ---
    signal(SIGINT,  signalHandler);
    signal(SIGTERM, signalHandler);

    // --- Optional timing logger ---
    std::unique_ptr<TimingLogger> timingLogger;
    if (cfg.debug_timing) {
        timingLogger = std::make_unique<TimingLogger>(cfg.timing_log_path);
        std::cout << "  Timing log:    " << cfg.timing_log_path << "\n";
    }

    // --- Create components ---
    auto fb = std::make_unique<FrameBuffer>();
    UDPReceiver receiver(*fb, cfg.udp_port, timingLogger.get());
    HallSensor  hall(cfg);
    DMAOutput   leds(*fb, hall, cfg, timingLogger.get());

    // --- Start everything ---
    if (!receiver.start()) {
        std::cerr << "Failed to start UDP receiver\n";
        return 1;
    }
    // leds must start before hall: librgbmatrix initialises the BCM GPIO
    // hardware registers (via /dev/mem) which clears edge-detection bits for
    // all pins.  Starting leds first ensures those registers are stable before
    // gpiod begins polling GPIO 26 for hall-sensor edges.
    if (!leds.start()) {
        std::cerr << "Failed to start LED output\n";
        return 1;
    }
    if (!hall.start()) {
        std::cerr << "Failed to start hall sensor\n";
        return 1;
    }

    // --- Main thread just waits for shutdown signal ---
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "\nShutting down...\n";
    fb->shutdown();   // wake LEDOutput thread if blocked in acquireRead()
    hall.stop();
    leds.stop();
    receiver.stop();

    return 0;
}
