#include "HallSensor.h"
#include <gpiod.h>
#include <iostream>
#include <chrono>

HallSensor::HallSensor(int gpioPin)
    : gpioPin_(gpioPin) {}

HallSensor::~HallSensor() {
    stop();
}

void HallSensor::setCallback(Callback cb) {
    callback_ = std::move(cb);
}

bool HallSensor::start() {
    running_ = true;
    thread_  = std::thread(&HallSensor::run, this);
    return true;
}

void HallSensor::stop() {
    running_ = false;
    if (thread_.joinable())
        thread_.join();
}

int64_t HallSensor::lastRotationUs() const {
    return lastRotationUs_.load(std::memory_order_relaxed);
}

double HallSensor::rpm() const {
    int64_t us = lastRotationUs();
    if (us <= 0) return 0.0;
    // RPM = 60s / period
    return 60'000'000.0 / static_cast<double>(us);
}

void HallSensor::run() {
    gpiod_chip* chip = gpiod_chip_open("/dev/gpiochip0");
    if (!chip) {
        std::cerr << "HallSensor: cannot open gpiochip0\n";
        running_ = false;
        return;
    }

    gpiod_line_settings* settings = gpiod_line_settings_new();
    if (!settings) {
        std::cerr << "HallSensor: cannot create line settings\n";
        gpiod_chip_close(chip);
        running_ = false;
        return;
    }
    gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_INPUT);
    gpiod_line_settings_set_edge_detection(settings, GPIOD_LINE_EDGE_RISING);

    gpiod_line_config* line_cfg = gpiod_line_config_new();
    if (!line_cfg) {
        std::cerr << "HallSensor: cannot create line config\n";
        gpiod_line_settings_free(settings);
        gpiod_chip_close(chip);
        running_ = false;
        return;
    }

    unsigned int offset = static_cast<unsigned int>(gpioPin_);
    gpiod_line_config_add_line_settings(line_cfg, &offset, 1, settings);
    gpiod_line_settings_free(settings);

    gpiod_request_config* req_cfg = gpiod_request_config_new();
    if (req_cfg)
        gpiod_request_config_set_consumer(req_cfg, "hologram_hall");

    gpiod_line_request* request = gpiod_chip_request_lines(chip, req_cfg, line_cfg);
    gpiod_request_config_free(req_cfg);
    gpiod_line_config_free(line_cfg);

    if (!request) {
        std::cerr << "HallSensor: cannot request events on GPIO " << gpioPin_ << "\n";
        gpiod_chip_close(chip);
        running_ = false;
        return;
    }

    std::cout << "HallSensor: monitoring GPIO " << gpioPin_ << "\n";

    gpiod_edge_event_buffer* event_buf = gpiod_edge_event_buffer_new(1);
    if (!event_buf) {
        std::cerr << "HallSensor: cannot create event buffer\n";
        gpiod_line_request_release(request);
        gpiod_chip_close(chip);
        running_ = false;
        return;
    }

    using Clock = std::chrono::steady_clock;
    auto lastEdge = Clock::now();
    bool firstEdge = true;

    while (running_) {
        // Wait up to 100ms for an edge event
        int ret = gpiod_line_request_wait_edge_events(request, 100'000'000);
        if (ret < 0) {
            std::cerr << "HallSensor: event wait error\n";
            break;
        }
        if (ret == 0)
            continue; // timeout, check running_ and loop

        int num = gpiod_line_request_read_edge_events(request, event_buf, 1);
        if (num < 0)
            continue;

        auto now = Clock::now();
        if (!firstEdge) {
            auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                          now - lastEdge).count();
            lastRotationUs_.store(us, std::memory_order_relaxed);
            if (callback_)
                callback_(us);
        }
        firstEdge = false;
        lastEdge  = now;
    }

    gpiod_edge_event_buffer_free(event_buf);
    gpiod_line_request_release(request);
    gpiod_chip_close(chip);
}
