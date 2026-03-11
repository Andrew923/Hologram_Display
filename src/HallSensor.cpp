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
    gpiod_chip* chip = gpiod_chip_open_by_name("gpiochip0");
    if (!chip) {
        std::cerr << "HallSensor: cannot open gpiochip0\n";
        running_ = false;
        return;
    }

    gpiod_line* line = gpiod_chip_get_line(chip, gpioPin_);
    if (!line) {
        std::cerr << "HallSensor: cannot get GPIO " << gpioPin_ << "\n";
        gpiod_chip_close(chip);
        running_ = false;
        return;
    }

    // Request rising-edge events
    if (gpiod_line_request_rising_edge_events(line, "hologram_hall") < 0) {
        std::cerr << "HallSensor: cannot request events on GPIO " << gpioPin_ << "\n";
        gpiod_chip_close(chip);
        running_ = false;
        return;
    }

    std::cout << "HallSensor: monitoring GPIO " << gpioPin_ << "\n";

    using Clock = std::chrono::steady_clock;
    auto lastEdge = Clock::now();
    bool firstEdge = true;

    timespec timeout{0, 100'000'000}; // 100 ms poll timeout

    while (running_) {
        int ret = gpiod_line_event_wait(line, &timeout);
        if (ret < 0) {
            std::cerr << "HallSensor: event wait error\n";
            break;
        }
        if (ret == 0)
            continue; // timeout, check running_ and loop

        gpiod_line_event event;
        if (gpiod_line_event_read(line, &event) < 0)
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

    gpiod_line_release(line);
    gpiod_chip_close(chip);
}
