#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <fstream>
#include <string>
#include <thread>

// Lock-free ring-buffer timing logger.
//
// Multiple threads may call log() concurrently.  Entries are accumulated in a
// fixed-size ring (dropped if full) and flushed to a CSV file by a background
// thread every 500 ms, keeping file I/O off the real-time DMA thread.
//
// CSV format:
//   timestamp_us,event,value_us
class TimingLogger {
public:
    struct Entry {
        int64_t     timestamp_us;  // CLOCK_MONOTONIC at time of log() call
        const char* event;         // static string literal — no allocation
        int64_t     value_us;      // measured duration or interval
    };

    explicit TimingLogger(const std::string& path);
    ~TimingLogger();  // signals flush thread to stop, drains remaining entries

    // Record one timing event.  Thread-safe; never blocks; drops entry if ring full.
    void log(const char* event, int64_t value_us);

    // Current monotonic time in microseconds (shared with callers for cheapness).
    static int64_t nowUs();

private:
    static constexpr size_t RING_SIZE = 8192;  // must be power of two
    static constexpr size_t RING_MASK = RING_SIZE - 1;

    std::array<Entry, RING_SIZE> ring_;
    std::atomic<size_t>          writeIdx_{0};
    std::atomic<size_t>          readIdx_{0};

    std::ofstream      file_;
    std::thread        flushThread_;
    std::atomic<bool>  running_{true};

    void flushLoop();
    void drain();  // writes all pending ring entries to file_
};
