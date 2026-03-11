// Hall Effect Sensor Test
// Monitors the hall sensor GPIO pin and prints timing / RPM data.
// Run with:  sudo ./build/test_hall [config_path]

#include "Config.h"
#include <gpiod.h>
#include <signal.h>
#include <iostream>
#include <chrono>
#include <cstdint>

static volatile bool g_running = true;
static void sigHandler(int) { g_running = false; }

int main(int argc, char* argv[]) {
    signal(SIGINT, sigHandler);
    signal(SIGTERM, sigHandler);

    Config cfg;
    if (argc > 1)
        cfg.loadFromFile(argv[1]);
    else
        cfg.loadFromFile("config/default.cfg");

    int pin = cfg.hall_gpio_pin;
    std::cout << "Hall Effect Test: monitoring GPIO " << pin << "\n"
              << "Press Ctrl+C to stop.\n\n";

    gpiod_chip* chip = gpiod_chip_open_by_name("gpiochip0");
    if (!chip) {
        std::cerr << "Cannot open gpiochip0. Are you running as root?\n";
        return 1;
    }

    gpiod_line* line = gpiod_chip_get_line(chip, pin);
    if (!line) {
        std::cerr << "Cannot get GPIO line " << pin << "\n";
        gpiod_chip_close(chip);
        return 1;
    }

    if (gpiod_line_request_rising_edge_events(line, "test_hall") < 0) {
        std::cerr << "Cannot request events on GPIO " << pin
                  << ". Check wiring and permissions.\n";
        gpiod_chip_close(chip);
        return 1;
    }

    std::cout << "Waiting for hall sensor edges...\n\n";

    using Clock = std::chrono::steady_clock;
    auto lastEdge = Clock::now();
    bool firstEdge = true;
    int  edgeCount = 0;

    timespec timeout{1, 0}; // 1 second poll timeout

    while (g_running) {
        int ret = gpiod_line_event_wait(line, &timeout);

        if (ret < 0) {
            std::cerr << "Event wait error\n";
            break;
        }

        if (ret == 0) {
            // Timeout — print status
            if (firstEdge) {
                std::cout << "  (no edges detected yet — check wiring)\n";
            }
            continue;
        }

        gpiod_line_event event;
        if (gpiod_line_event_read(line, &event) < 0)
            continue;

        edgeCount++;
        auto now = Clock::now();

        if (firstEdge) {
            std::cout << "  First edge detected!\n";
            firstEdge = false;
        } else {
            auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                          now - lastEdge).count();
            double ms  = us / 1000.0;
            double rpm = (us > 0) ? 60'000'000.0 / us : 0;

            std::cout << "  Edge #" << edgeCount
                      << "  period=" << ms << " ms"
                      << "  RPM=" << rpm
                      << "\n";
        }

        lastEdge = now;
    }

    std::cout << "\nTotal edges detected: " << edgeCount << "\n";

    gpiod_line_release(line);
    gpiod_chip_close(chip);

    std::cout << "Hall test complete.\n";
    return 0;
}
