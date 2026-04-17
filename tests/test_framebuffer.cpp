#include "FrameBuffer.h"

#include <array>
#include <cassert>
#include <future>
#include <memory>
#include <thread>

int main() {
    auto fb = std::make_unique<FrameBuffer>();

    // No committed frame yet.
    assert(fb->tryAcquireRead() == nullptr);

    std::array<RGB, PIXELS_PER_SLICE> pixels{};
    for (size_t i = 0; i < pixels.size(); ++i)
        pixels[i] = RGB{static_cast<uint8_t>(i & 0xFF), 0x55, 0xAA};

    fb->writeSlice(0, pixels.data());
    fb->commitWrite();

    const FrameSet* frame = fb->tryAcquireRead();
    assert(frame != nullptr);
    assert(frame->slices[0][0].r == pixels[0].r);
    assert(frame->slices[0][0].g == pixels[0].g);
    assert(frame->slices[0][0].b == pixels[0].b);

    // No newer frame committed since last acquisition.
    assert(fb->tryAcquireRead() == nullptr);

    // shutdown() should wake acquireRead waiters cleanly.
    auto waiter = std::async(std::launch::async, [&fb]() {
        return fb->acquireRead();
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    fb->shutdown();
    assert(waiter.get() == nullptr);

    return 0;
}
