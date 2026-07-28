#include "PixelFont.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <string.h>

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

// Count UTF-8 codepoints in a NUL-terminated string. (strlen gives bytes, which
// over-counts when the charset contains multi-byte chars like '°', '€', etc.)
int count_utf8_codepoints(const char* s) {
    int n = 0;
    while (*s) {
        uint8_t c = (uint8_t)*s;
        int len;
        if      (c < 0x80)         len = 1;
        else if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        else                         len = 1;  // malformed, skip one byte
        s += len;
        n++;
    }
    return n;
}

}  // namespace

PixelFont::PixelFont() = default;

PixelFont::~PixelFont() {
    if (_owned) delete[] _pixels;
}

bool PixelFont::loadFromFile(const char* path,
                             uint8_t charW, uint8_t charH,
                             const char* charset,
                             uint8_t tr, uint8_t tg, uint8_t tb) {
    if (_owned) delete[] _pixels;   // never delete[] a caller-owned BSS buffer
    _pixels   = nullptr;
    _owned    = false;
    _charW    = 0;
    _charH    = 0;
    _numChars = 0;
    _charset  = nullptr;

    File f = LittleFS.open(path, "r");
    if (!f) {
        LOG_SERROR("[PixelFont] open failed: %s", path);
        return false;
    }

    if (f.read() != 'B' || f.read() != 'M') {
        LOG_SERROR("[PixelFont] not a BMP file");
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

    if (bpp != 24) {
        LOG_SERROR("[PixelFont] expected 24bpp, got %ubpp", bpp);
        f.close();
        return false;
    }

    bool topDown   = (height < 0);
    int32_t absH   = topDown ? -height : height;

    if (width <= 0 || absH <= 0 || width % charW != 0 || absH % charH != 0) {
        LOG_SERROR("[PixelFont] bad atlas dimensions %ldx%ld for char %ux%u",
                      (long)width, (long)absH, charW, charH);
        f.close();
        return false;
    }

    int cols = (int)(width / charW);
    int rows = (int)(absH / charH);
    int totalSlots = cols * rows;
    int csLen = count_utf8_codepoints(charset);
    if (csLen <= 0) {
        LOG_SERROR("[PixelFont] empty charset");
        f.close();
        return false;
    }
    if (csLen > totalSlots) {
        LOG_SERROR("[PixelFont] charset has %d chars but atlas only has %d slots",
                      csLen, totalSlots);
        f.close();
        return false;
    }

    int rowBytes = (((int)width * 3) + 3) & ~3;  // 4-byte aligned BMP row

    int pixelsPerChar = (int)charW * (int)charH;
    int totalPixels   = csLen * pixelsPerChar;
    _pixels = new (std::nothrow) uint16_t[totalPixels];
    if (!_pixels) {
        LOG_SERROR("[PixelFont] alloc failed (%d uint16_t)", totalPixels);
        f.close();
        return false;
    }
    _owned = true;

    uint8_t* rowBuf = new (std::nothrow) uint8_t[rowBytes];
    if (!rowBuf) {
        LOG_SERROR("[PixelFont] rowBuf alloc failed");
        delete[] _pixels;
        _pixels = nullptr;
        f.close();
        return false;
    }

    for (int imgY = 0; imgY < absH; imgY++) {
        int bmpRow = topDown ? imgY : (absH - 1 - imgY);
        f.seek(pixelOffset + (uint32_t)bmpRow * rowBytes);
        f.read(rowBuf, rowBytes);

        int gridRow = imgY / charH;
        int py      = imgY % charH;

        for (int imgX = 0; imgX < width; imgX++) {
            int gridCol = imgX / charW;
            int px      = imgX % charW;
            int idx     = gridRow * cols + gridCol;
            if (idx >= csLen) continue;  // beyond charset -> skip

            uint8_t b = rowBuf[imgX * 3 + 0];
            uint8_t g = rowBuf[imgX * 3 + 1];
            uint8_t r = rowBuf[imgX * 3 + 2];

            uint16_t pixel;
            if (r == tr && g == tg && b == tb) {
                pixel = TRANSPARENT_RGB565;
            } else {
                pixel = bgr888_to_rgb565(b, g, r);
            }

            _pixels[idx * pixelsPerChar + py * charW + px] = pixel;
        }
    }

    delete[] rowBuf;
    f.close();

    _charW    = charW;
    _charH    = charH;
    _numChars = (uint16_t)csLen;
    _charset  = charset;

    LOG_SDEBUG("[PixelFont] loaded %s: atlas %ldx%ld, %u chars %ux%u px, transp=%02X%02X%02X",
                  path, (long)width, (long)absH, _numChars, _charW, _charH, tr, tg, tb);
    return true;
}

bool PixelFont::copyScaledFrom(const PixelFont& src, uint8_t scale) {
    return copyScaledFrom(src, scale, scale);
}

bool PixelFont::copyScaledFrom(const PixelFont& src, uint8_t scaleX, uint8_t scaleY,
                                uint16_t* buf, uint32_t bufWords) {
    if (!src.isLoaded() || scaleX < 1 || scaleY < 1 || !buf) return false;
    if (&src == this) {
        LOG_SERROR("[PixelFont] copyScaledFrom: self-scale not supported");
        return false;
    }

    const uint16_t newW = (uint16_t)src._charW * scaleX;
    const uint16_t newH = (uint16_t)src._charH * scaleY;
    if (newW > 255 || newH > 255) {
        LOG_SERROR("[PixelFont] scaled char too large: %ux%u", newW, newH);
        return false;
    }

    const uint32_t needed = (uint32_t)src._numChars * newW * newH;
    if (bufWords < needed) {
        LOG_SERROR("[PixelFont] buf too small: need %u, got %u uint16_t",
                      (unsigned)needed, (unsigned)bufWords);
        return false;
    }

    // Fill the buffer BEFORE claiming ownership so that the object is never
    // visible in a half-initialized state (isLoaded()=true but _charW=0).
    for (uint16_t c = 0; c < src._numChars; c++) {
        const uint16_t* srcGlyph = src.glyphPixels(c);
        uint16_t*       dstGlyph = buf + (uint32_t)c * newW * newH;
        for (uint8_t syi = 0; syi < src._charH; syi++) {
            for (uint8_t sxi = 0; sxi < src._charW; sxi++) {
                uint16_t pix = srcGlyph[(uint32_t)syi * src._charW + sxi];
                for (uint8_t dy = 0; dy < scaleY; dy++) {
                    uint32_t outY = (uint32_t)syi * scaleY + dy;
                    for (uint8_t dx = 0; dx < scaleX; dx++) {
                        uint32_t outX = (uint32_t)sxi * scaleX + dx;
                        dstGlyph[outY * newW + outX] = pix;
                    }
                }
            }
        }
    }

    // Commit: set metadata first, then publish _pixels so isLoaded() only
    // flips to true when the font is fully consistent.
    _charW    = (uint8_t)newW;
    _charH    = (uint8_t)newH;
    _numChars = src._numChars;
    _charset  = src._charset;

    if (_owned) delete[] _pixels;
    _pixels = buf;
    _owned  = false;

    LOG_SDEBUG("[PixelFont] scaled %ux%u -> %ux%u (%ux%u factor, %u bytes, BSS)",
                  src._charW, src._charH, _charW, _charH, scaleX, scaleY,
                  (unsigned)(needed * sizeof(uint16_t)));
    return true;
}

bool PixelFont::copyScaledFrom(const PixelFont& src, uint8_t sx, uint8_t sy) {
    if (!src.isLoaded() || sx < 1 || sy < 1) return false;
    if (&src == this) {
        LOG_SERROR("[PixelFont] copyScaledFrom: self-scale not supported");
        return false;
    }

    const uint16_t newW = (uint16_t)src._charW * sx;
    const uint16_t newH = (uint16_t)src._charH * sy;
    if (newW > 255 || newH > 255) {
        LOG_SERROR("[PixelFont] scaled char too large: %ux%u", newW, newH);
        return false;
    }

    if (_owned) delete[] _pixels;
    _pixels = nullptr;
    _owned  = false;

    const int pixelsPerChar = (int)newW * (int)newH;
    const int total         = (int)src._numChars * pixelsPerChar;
    _pixels = new (std::nothrow) uint16_t[total];
    if (!_pixels) {
        LOG_SERROR("[PixelFont] scale alloc failed (%d uint16_t)", total);
        return false;
    }
    _owned = true;

    for (uint16_t c = 0; c < src._numChars; c++) {
        const uint16_t* srcGlyph = src.glyphPixels(c);
        uint16_t*       dstGlyph = _pixels + (uint32_t)c * pixelsPerChar;
        for (uint8_t syi = 0; syi < src._charH; syi++) {
            for (uint8_t sxi = 0; sxi < src._charW; sxi++) {
                uint16_t pix = srcGlyph[(uint32_t)syi * src._charW + sxi];
                for (uint8_t dy = 0; dy < sy; dy++) {
                    uint16_t outY = (uint16_t)syi * sy + dy;
                    for (uint8_t dx = 0; dx < sx; dx++) {
                        uint16_t outX = (uint16_t)sxi * sx + dx;
                        dstGlyph[(uint32_t)outY * newW + outX] = pix;
                    }
                }
            }
        }
    }

    _charW    = (uint8_t)newW;
    _charH    = (uint8_t)newH;
    _numChars = src._numChars;
    _charset  = src._charset;

    LOG_SINFO("[PixelFont] scaled %ux%u -> %ux%u (%ux%u factor, %d bytes)",
                  src._charW, src._charH, _charW, _charH, sx, sy,
                  total * 2);
    return true;
}
