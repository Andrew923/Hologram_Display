#include "TimingLogger.h"

#include <ctime>
#include <iostream>

int64_t TimingLogger::nowUs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1'000'000
         + static_cast<int64_t>(ts.tv_nsec) / 1'000;
}

TimingLogger::TimingLogger(const std::string& path)
    : file_(path, std::ios::out | std::ios::trunc)
{
    if (!file_.is_open()) {
        std::cerr << "TimingLogger: cannot open " << path << "\n";
        return;
    }
    file_ << "timestamp_us,event,value_us\n";
    flushThread_ = std::thread(&TimingLogger::flushLoop, this);
    std::cout << "TimingLogger: writing to " << path << "\n";
}

TimingLogger::~TimingLogger() {
    running_ = false;
    if (flushThread_.joinable())
        flushThread_.join();
    drain();
    file_.flush();
}

void TimingLogger::log(const char* event, int64_t value_us) {
    // Claim a slot with a fetch_add.  Two concurrent producers may write to
    // different indices simultaneously — this is safe because each index is
    // only written once before the readIdx catches up.
    size_t idx = writeIdx_.fetch_add(1, std::memory_order_relaxed) & RING_MASK;

    // If the ring is full (producer lapped consumer) we still write but the
    // drain() will see a gap; we accept occasional loss rather than blocking.
    ring_[idx] = {nowUs(), event, value_us};

    // Advance writeIdx is already done via fetch_add; nothing else to do.
}

void TimingLogger::flushLoop() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        drain();
    }
}

void TimingLogger::drain() {
    if (!file_.is_open()) return;

    size_t r = readIdx_.load(std::memory_order_relaxed);
    size_t w = writeIdx_.load(std::memory_order_acquire);

    while (r != w) {
        const Entry& e = ring_[r & RING_MASK];
        file_ << e.timestamp_us << ',' << e.event << ',' << e.value_us << '\n';
        r++;
    }
    readIdx_.store(r, std::memory_order_relaxed);
    file_.flush();
}
