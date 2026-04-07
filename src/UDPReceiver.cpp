#include "UDPReceiver.h"
#include "Protocol.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <iostream>

// Maximum expected UDP datagram size.  A fully-uncompressible 128x64 slice
// would be 2 + 128*64*4 = 32770 bytes; 64 KiB is generous.
static constexpr size_t MAX_DGRAM = 65536;

UDPReceiver::UDPReceiver(FrameBuffer& fb, int port, TimingLogger* logger)
    : fb_(fb), port_(port), logger_(logger) {}

UDPReceiver::~UDPReceiver() {
    stop();
}

bool UDPReceiver::start() {
    sockfd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd_ < 0) {
        perror("UDPReceiver: socket");
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(static_cast<uint16_t>(port_));
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sockfd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("UDPReceiver: bind");
        close(sockfd_);
        sockfd_ = -1;
        return false;
    }

    // Set a receive timeout so we can check running_ periodically
    timeval tv{};
    tv.tv_sec  = 0;
    tv.tv_usec = 100000; // 100 ms
    setsockopt(sockfd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    running_ = true;
    thread_  = std::thread(&UDPReceiver::run, this);

    std::cout << "UDPReceiver: listening on port " << port_ << "\n";
    return true;
}

void UDPReceiver::stop() {
    running_ = false;
    if (thread_.joinable())
        thread_.join();
    if (sockfd_ >= 0) {
        close(sockfd_);
        sockfd_ = -1;
    }
}

void UDPReceiver::run() {
    uint8_t buf[MAX_DGRAM];
    Packet pkt;
    int64_t lastRecvUs = 0;

    while (running_) {
        ssize_t n = recv(sockfd_, buf, sizeof(buf), 0);
        if (n <= 0)
            continue; // timeout or error — just retry

        int64_t tRecv = logger_ ? TimingLogger::nowUs() : 0;

        if (logger_) {
            if (lastRecvUs > 0)
                logger_->log("udp_inter_packet_us", tRecv - lastRecvUs);
            lastRecvUs = tRecv;
        }

        if (!decodePacket(buf, static_cast<size_t>(n), pkt)) {
            std::cerr << "UDPReceiver: malformed packet (" << n << " bytes)\n";
            continue;
        }

        if (logger_) logger_->log("udp_decode_us", TimingLogger::nowUs() - tRecv);

        if (pkt.flag == FLAG_SLICE) {
            int64_t tWrite = logger_ ? TimingLogger::nowUs() : 0;
            fb_.writeSlice(pkt.id, pkt.pixels);
            if (logger_) logger_->log("udp_write_slice_us", TimingLogger::nowUs() - tWrite);

            receivedCount_++;

            // Once we have a full set of slices, commit the frame.
            if (receivedCount_ >= SLICE_COUNT) {
                int64_t tCommit = logger_ ? TimingLogger::nowUs() : 0;
                fb_.commitWrite();
                if (logger_) logger_->log("udp_frame_commit_us", TimingLogger::nowUs() - tCommit);
                receivedCount_ = 0;
            }
        }
        // FLAG_HAND_FRAME could be handled here in the future
    }
}
