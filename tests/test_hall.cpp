// Hall Effect Sensor Test
// Monitors the hall sensor GPIO pin and prints timing / RPM data.
//
// Usage:  sudo ./build/test_hall [config_path] [--edge=falling|rising|both] [--bias=pull_up|pull_down|none]
//
// Defaults (from config or built-in): bias=pull_up, edge=falling
// These defaults suit the A3144 open-collector module, which pulls the line
// LOW when a magnet is present.  If the module has an inverting driver board
// use --edge=rising instead.

#include "Config.h"
#include <gpiod.h>
#include <signal.h>
#include <iostream>
#include <chrono>
#include <cstdint>
#include <string>

static volatile bool g_running = true;
static void sigHandler(int) { g_running = false; }

// ── helpers ────────────────────────────────────────────────────────────────

static gpiod_line_bias parseBias(const std::string& s) {
    if (s == "pull_up")   return GPIOD_LINE_BIAS_PULL_UP;
    if (s == "pull_down") return GPIOD_LINE_BIAS_PULL_DOWN;
    if (s == "none")      return GPIOD_LINE_BIAS_DISABLED;
    std::cerr << "Unknown bias '" << s << "', using pull_up\n";
    return GPIOD_LINE_BIAS_PULL_UP;
}

static gpiod_line_edge parseEdge(const std::string& s) {
    if (s == "falling") return GPIOD_LINE_EDGE_FALLING;
    if (s == "rising")  return GPIOD_LINE_EDGE_RISING;
    if (s == "both")    return GPIOD_LINE_EDGE_BOTH;
    std::cerr << "Unknown edge '" << s << "', using falling\n";
    return GPIOD_LINE_EDGE_FALLING;
}

static const char* edgeName(gpiod_edge_event_type t) {
    switch (t) {
        case GPIOD_EDGE_EVENT_RISING_EDGE:  return "rising";
        case GPIOD_EDGE_EVENT_FALLING_EDGE: return "falling";
        default:                            return "unknown";
    }
}

// ── main ───────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    signal(SIGINT, sigHandler);
    signal(SIGTERM, sigHandler);

    // --- Load configuration (first positional arg or default path) ---
    Config cfg;
    int    firstFlag = 1; // index of first '--' argument
    if (argc > 1 && argv[1][0] != '-') {
        cfg.loadFromFile(argv[1]);
        firstFlag = 2;
    } else {
        cfg.loadFromFile("config/default.cfg");
    }

    // --- Parse optional CLI overrides: --edge=X --bias=X ---
    for (int i = firstFlag; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.rfind("--edge=", 0) == 0)
            cfg.hall_edge = arg.substr(7);
        else if (arg.rfind("--bias=", 0) == 0)
            cfg.hall_bias = arg.substr(7);
        else
            std::cerr << "Unknown argument '" << arg << "' (ignored)\n";
    }

    int         pin  = cfg.hall_gpio_pin;
    std::string bias = cfg.hall_bias;
    std::string edge = cfg.hall_edge;

    std::cout << "Hall Effect Test\n"
              << "  GPIO " << pin
              << "  bias=" << bias
              << "  edge=" << edge << "\n"
              << "Press Ctrl+C to stop.\n\n";

    // --- Open GPIO chip ---
    gpiod_chip* chip = gpiod_chip_open("/dev/gpiochip0");
    if (!chip) {
        std::cerr << "Cannot open gpiochip0. Are you running as root?\n";
        return 1;
    }

    // --- Configure line: input + explicit bias + edge detection ---
    gpiod_line_settings* settings = gpiod_line_settings_new();
    if (!settings) {
        std::cerr << "Cannot create line settings\n";
        gpiod_chip_close(chip);
        return 1;
    }
    gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_INPUT);
    gpiod_line_settings_set_bias(settings, parseBias(bias));
    gpiod_line_settings_set_edge_detection(settings, parseEdge(edge));

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
    auto lastEdge  = Clock::now();
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
            // Timeout — print current line level and configuration for debug
            int level = gpiod_line_request_get_value(request, offset);
            if (level < 0) {
                std::cerr << "  (timeout: failed to read GPIO level — check permissions)\n";
            } else {
                std::cout << "  (timeout: GPIO level="
                          << (level == GPIOD_LINE_VALUE_ACTIVE ? "HIGH" : "LOW")
                          << "  bias=" << bias
                          << "  edge=" << edge;
                if (firstEdge)
                    std::cout << "  — no edges detected yet, check wiring";
                std::cout << ")\n";
            }
            continue;
        }

        int num = gpiod_line_request_read_edge_events(request, event_buf, 1);
        if (num < 0)
            continue;

        edgeCount++;
        auto now = Clock::now();

        // Get the event type so we can label it in "both" mode
        gpiod_edge_event* ev = gpiod_edge_event_buffer_get_event(event_buf, 0);
        const char* evName   = ev ? edgeName(gpiod_edge_event_get_event_type(ev))
                                  : edge.c_str();

        if (firstEdge) {
            std::cout << "  First edge detected! (" << evName << ")\n";
            firstEdge = false;
        } else {
            auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                          now - lastEdge).count();
            double ms  = us / 1000.0;
            double rpm = (us > 0) ? 60'000'000.0 / us : 0;

            std::cout << "  Edge #" << edgeCount
                      << " (" << evName << ")"
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

