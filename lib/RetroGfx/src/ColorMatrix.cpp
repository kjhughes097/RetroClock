#include "ColorMatrix.h"

#include <Arduino.h>
#include <LittleFS.h>

namespace {

uint16_t readLE16(File& f) {
    uint8_t b[2];
    f.read(b, 2);
    return (uint16_t)b[0] | ((uint16_t)b[1] << 8);
}

uint32_t readLE32(File& f) {
    uint8_t b[4];
    f.read(b, 4);
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

uint16_t bgr888_to_rgb565(uint8_t b, uint8_t g, uint8_t r) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

}  // namespace

bool ColorMatrix::loadFromFile(const char* path) {
    _loaded = false;

    File f = LittleFS.open(path, "r");
    if (!f) {
        LOG_SERROR("[ColorMatrix] open failed: %s", path);
        return false;
    }

    if (f.read() != 'B' || f.read() != 'M') {
        LOG_SERROR("[ColorMatrix] not a BMP file");
        f.close();
        return false;
    }

    f.seek(0x0A);
    uint32_t pixelOffset = readLE32(f);

    f.seek(0x12);
    int32_t width  = (int32_t)readLE32(f);
    int32_t height = (int32_t)readLE32(f);
    f.seek(0x1C);
    uint16_t bpp = readLE16(f);

    if (width != 32 || (height != 64 && height != -64) || bpp != 24) {
        LOG_SERROR("[ColorMatrix] unexpected BMP: %ldx%ld %ubpp",
                      (long)width, (long)height, bpp);
        f.close();
        return false;
    }

    bool topDown       = (height < 0);
    int32_t absH       = topDown ? -height : height;
    const int rowBytes = 32 * 3;  // 32 px × 3 bytes, already 4-byte aligned

    uint8_t rowBuf[rowBytes];
    for (int32_t y = 0; y < absH; y++) {
        int32_t bmpRow = topDown ? y : (absH - 1 - y);
        f.seek(pixelOffset + (uint32_t)bmpRow * rowBytes);
        f.read(rowBuf, rowBytes);

        for (int32_t x = 0; x < 32; x++) {
            uint8_t b = rowBuf[x * 3 + 0];
            uint8_t g = rowBuf[x * 3 + 1];
            uint8_t r = rowBuf[x * 3 + 2];

            uint8_t col      = (uint8_t)(x / 8);
            uint8_t row      = (uint8_t)(y / 8);
            uint8_t tileIdx  = row * 4 + col;
            uint8_t px       = (uint8_t)(x % 8);
            uint8_t py       = (uint8_t)(y % 8);
            _tiles[tileIdx][py * 8 + px] = bgr888_to_rgb565(b, g, r);
        }
    }

    f.close();
    _loaded = true;
    LOG_SINFO("[ColorMatrix] loaded: 32 tiles (8x8 px each)");
    return true;
}
