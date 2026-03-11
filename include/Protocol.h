#pragma once

#include <cstdint>
#include <cstddef>

// Matches the Hologram engine's slicer constants
static constexpr int SLICE_COUNT = 120;
static constexpr int SLICE_W     = 128;
static constexpr int SLICE_H     = 64;
static constexpr int PIXELS_PER_SLICE = SLICE_W * SLICE_H;

// Packet type flags (second byte of every UDP packet)
static constexpr uint8_t FLAG_SLICE      = 0x00;
static constexpr uint8_t FLAG_HAND_FRAME = 0x01;

struct RGB {
    uint8_t r, g, b;
};

struct Packet {
    uint8_t  id;                       // slice or frame id
    uint8_t  flag;                     // FLAG_SLICE or FLAG_HAND_FRAME
    RGB      pixels[PIXELS_PER_SLICE]; // decoded pixel data
    bool     valid = false;
};

// Decode an incoming UDP datagram into a Packet.
// Returns false if the packet is malformed.
bool decodePacket(const uint8_t* data, size_t len, Packet& out);

// Decode column-major RLE (flag 0x00) into pixel array.
bool decodeRLE_ColMajor(const uint8_t* rle, size_t rleLen, RGB* out, int w, int h);

// Decode row-major RLE (flag 0x01) into pixel array.
bool decodeRLE_RowMajor(const uint8_t* rle, size_t rleLen, RGB* out, int w, int h);
