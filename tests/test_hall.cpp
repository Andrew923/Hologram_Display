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

    gpiod_chip* chip = gpiod_chip_open("/dev/gpiochip0");
    if (!chip) {
        std::cerr << "Cannot open gpiochip0. Are you running as root?\n";
        return 1;
    }

    gpiod_line_settings* settings = gpiod_line_settings_new();
    if (!settings) {
        std::cerr << "Cannot create line settings\n";
        gpiod_chip_close(chip);
        return 1;
    }
    gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_INPUT);
    gpiod_line_settings_set_edge_detection(settings, GPIOD_LINE_EDGE_RISING);

    gpiod_line_config* line_cfg = gpiod_line_config_new();
    if (!line_cfg) {
        std::cerr << "Cannot create line config\n";
        gpiod_line_settings_free(settings);
        gpiod_chip_close(chip);
        return 1;
    }

    unsigned int offset = static_cast<unsigned int>(pin);
    gpiod_line_config_add_line_settings(line_cfg, &offset, 1, settings);
    gpiod_line_settings_free(settings);

    gpiod_request_config* req_cfg = gpiod_request_config_new();
    if (req_cfg)
        gpiod_request_config_set_consumer(req_cfg, "test_hall");

    gpiod_line_request* request = gpiod_chip_request_lines(chip, req_cfg, line_cfg);
    gpiod_request_config_free(req_cfg);
    gpiod_line_config_free(line_cfg);

    if (!request) {
        std::cerr << "Cannot request events on GPIO " << pin
                  << ". Check wiring and permissions.\n";
        gpiod_chip_close(chip);
        return 1;
    }

    gpiod_edge_event_buffer* event_buf = gpiod_edge_event_buffer_new(1);
    if (!event_buf) {
        std::cerr << "Cannot create event buffer\n";
        gpiod_line_request_release(request);
        gpiod_chip_close(chip);
        return 1;
    }

    std::cout << "Waiting for hall sensor edges...\n\n";

    using Clock = std::chrono::steady_clock;
    auto lastEdge = Clock::now();
    bool firstEdge = true;
    int  edgeCount = 0;

    while (g_running) {
        // Wait up to 1 second for an edge event
        int ret = gpiod_line_request_wait_edge_events(request, 1'000'000'000LL);

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

        int num = gpiod_line_request_read_edge_events(request, event_buf, 1);
        if (num < 0)
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

    gpiod_edge_event_buffer_free(event_buf);
    gpiod_line_request_release(request);
    gpiod_chip_close(chip);

    std::cout << "Hall test complete.\n";
    return 0;
}
