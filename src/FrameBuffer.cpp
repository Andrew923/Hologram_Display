#include "FrameBuffer.h"
#include <cstring>

FrameBuffer::FrameBuffer() {
    for (auto& buf : buffers_)
        buf.clear();
}

void FrameBuffer::writeSlice(int sliceId, const RGB* pixels) {
    if (sliceId < 0 || sliceId >= SLICE_COUNT)
        return;

    auto& buf = buffers_[writeIdx_];
    std::memcpy(buf.slices[sliceId].data(), pixels,
                PIXELS_PER_SLICE * sizeof(RGB));
    buf.slices_received.fetch_add(1, std::memory_order_relaxed);
}

void FrameBuffer::commitWrite() {
    std::lock_guard<std::mutex> lock(mutex_);

    // Swap write and ready: the just-completed write buffer becomes the new
    // "ready" buffer, and the old ready buffer becomes our next write target.
    std::swap(writeIdx_, readyIdx_);
    buffers_[writeIdx_].clear();
    hasReady_ = true;

    cv_.notify_one();
}

const FrameSet* FrameBuffer::acquireRead() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return hasReady_ || shutdown_.load(std::memory_order_relaxed); });

    if (!hasReady_)
        return nullptr; // woken by shutdown()

    // Swap ready and read: the reader takes ownership of the ready buffer.
    std::swap(readyIdx_, readIdx_);
    hasReady_ = false;

    return &buffers_[readIdx_];
}

const FrameSet* FrameBuffer::tryAcquireRead() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!hasReady_)
        return nullptr;

    // Swap ready and read: the reader takes ownership of the ready buffer.
    std::swap(readyIdx_, readIdx_);
    hasReady_ = false;

    return &buffers_[readIdx_];
}

void FrameBuffer::shutdown() {
    shutdown_.store(true, std::memory_order_relaxed);
    cv_.notify_all();
}
