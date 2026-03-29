#pragma once

#include "Config.h"
#include <atomic>
#include <thread>
#include <functional>

// Reads a hall effect sensor via Linux gpiod.
// On each detected edge (one full rotation), records the timestamp and
// computes angular velocity.  Bias and edge direction are taken from Config.
class HallSensor {
public:
    explicit HallSensor(const Config& cfg);
    ~HallSensor();

    bool start();
    void stop();

    // Get the duration of the most recent full rotation in microseconds.
    // Returns 0 if no rotation has been measured yet.
    int64_t lastRotationUs() const;

    // Get current estimated RPM.  Returns 0 if unknown.
    double rpm() const;

    // Optional callback fired on each rotation edge.
    using Callback = std::function<void(int64_t rotationUs)>;
    void setCallback(Callback cb);

private:
    void run();

    int  gpioPin_;
    std::string bias_;
    std::string edge_;
    std::atomic<bool>    running_{false};
    std::atomic<int64_t> lastRotationUs_{0};
    std::thread          thread_;
    Callback             callback_;
};
