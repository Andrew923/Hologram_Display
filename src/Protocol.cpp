#include "Protocol.h"
#include <cstring>

// Shared RLE decoding core. The difference between column-major and row-major
// is how the linear pixel index maps to (x, y).
static bool decodeRLE(const uint8_t* rle, size_t rleLen,
                      RGB* out, int totalPixels, bool colMajor, int w, int h) {
    int pixelIdx = 0;
    size_t pos = 0;

    while (pos + 3 < rleLen && pixelIdx < totalPixels) {
        uint8_t count = rle[pos];
        uint8_t r     = rle[pos + 1];
        uint8_t g     = rle[pos + 2];
        uint8_t b     = rle[pos + 3];
        pos += 4;

        if (count == 0)
            return false;

        for (int i = 0; i < count && pixelIdx < totalPixels; ++i, ++pixelIdx) {
            int x, y;
            if (colMajor) {
                // Column-major: column varies slowest, row varies fastest
                x = pixelIdx / h;
                y = pixelIdx % h;
            } else {
                // Row-major: row varies slowest, column varies fastest
                x = pixelIdx % w;
                y = pixelIdx / w;
            }
            out[y * w + x] = {r, g, b};
        }
    }

    return pixelIdx == totalPixels;
}

bool decodeRLE_ColMajor(const uint8_t* rle, size_t rleLen,
                        RGB* out, int w, int h) {
    return decodeRLE(rle, rleLen, out, w * h, true, w, h);
}

bool decodeRLE_RowMajor(const uint8_t* rle, size_t rleLen,
                        RGB* out, int w, int h) {
    return decodeRLE(rle, rleLen, out, w * h, false, w, h);
}

bool decodePacket(const uint8_t* data, size_t len, Packet& out) {
    // Minimum: 2 header bytes + at least 4 bytes of RLE
    if (len < 6) {
        out.valid = false;
        return false;
    }

    out.id   = data[0];
    out.flag = data[1];

    const uint8_t* rle    = data + 2;
    size_t         rleLen = len - 2;

    bool ok = false;
    if (out.flag == FLAG_SLICE) {
        ok = decodeRLE_ColMajor(rle, rleLen, out.pixels, SLICE_W, SLICE_H);
    } else if (out.flag == FLAG_HAND_FRAME) {
        ok = decodeRLE_RowMajor(rle, rleLen, out.pixels, SLICE_W, SLICE_H);
    }

    out.valid = ok;
    return ok;
}
