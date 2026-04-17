#pragma once

#include "Protocol.h"
#include <array>
#include <atomic>
#include <mutex>
#include <condition_variable>

// A complete set of decoded slices for one full rotation.
struct FrameSet {
    std::array<RGB, PIXELS_PER_SLICE> slices[SLICE_COUNT];
    std::atomic<int> slices_received{0};

    void clear() {
        slices_received.store(0, std::memory_order_relaxed);
    }
};

// Triple-buffer: the writer fills one FrameSet while the reader consumes
// another, and a third sits ready. This fully decouples the two threads.
class FrameBuffer {
public:
    FrameBuffer();

    // --- Writer (network thread) ---
    // Write a decoded slice into the current write buffer.
    void writeSlice(int sliceId, const RGB* pixels);

    // Mark the current write buffer as complete, swap it to be the latest
    // ready buffer.
    void commitWrite();

    // --- Reader (LED output thread) ---
    // Get a pointer to the most recently completed FrameSet.
    // Blocks until at least one full frame has been committed.
    // Returns nullptr if shutdown() was called before a frame was available.
    // The returned pointer is valid until the next call to acquireRead().
    const FrameSet* acquireRead();

    // Non-blocking variant of acquireRead().
    // Returns nullptr when no newer committed frame is available.
    // The returned pointer is valid until the next call to acquireRead()
    // or tryAcquireRead().
    const FrameSet* tryAcquireRead();

    // Wake any thread blocked in acquireRead() so it can exit cleanly.
    void shutdown();

private:
    static constexpr int BUF_COUNT = 3;
    FrameSet buffers_[BUF_COUNT];

    // Indices into buffers_[]
    int writeIdx_  = 0;
    int readyIdx_  = 1;
    int readIdx_   = 2;

    std::mutex              mutex_;
    std::condition_variable cv_;
    bool                    hasReady_ = false;
    std::atomic<bool>       shutdown_{false};
};
