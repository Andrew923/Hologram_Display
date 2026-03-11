#pragma once

#include "FrameBuffer.h"
#include <atomic>
#include <thread>

class UDPReceiver {
public:
    explicit UDPReceiver(FrameBuffer& fb, int port);
    ~UDPReceiver();

    bool start();
    void stop();

private:
    void run();

    FrameBuffer& fb_;
    int          port_;
    int          sockfd_ = -1;
    std::atomic<bool> running_{false};
    std::thread       thread_;

    // Track which slices we've received for the current frame
    int receivedCount_ = 0;
};
