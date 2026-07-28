#pragma once

#include <stdint.h>
#include "serlog.h"
// Multi-color bitmap font loaded at runtime from a 24-bit BMP atlas (chars in a
// regular left-to-right, top-to-bottom grid), decoded to RGB565 and kept in RAM.
// Transparency: source pixels matching a configurable RGB888 key (default magenta
// 0xFF00FF) are stored as TRANSPARENT_RGB565 (0xF81F); the render path checks it.
class PixelFont {
public:
    static constexpr uint16_t TRANSPARENT_RGB565 = 0xF81F;  // magenta sentinel

    PixelFont();
    ~PixelFont();

    PixelFont(const PixelFont&)            = delete;
    PixelFont& operator=(const PixelFont&) = delete;

    bool loadFromFile(const char* path,
                      uint8_t     charW,
                      uint8_t     charH,
                      const char* charset,
                      uint8_t     transparent_r = 0xFF,
                      uint8_t     transparent_g = 0x00,
                      uint8_t     transparent_b = 0xFF);

    // Nearest-neighbor integer-scaled copy of 'src' (factors >=1); new char size =
    // src size × scale. Charset is shared by pointer (must outlive this PixelFont).
    bool copyScaledFrom(const PixelFont& src, uint8_t scale);
    bool copyScaledFrom(const PixelFont& src, uint8_t scaleX, uint8_t scaleY);

    // Scale into a caller-owned buffer (BSS/static, no heap). bufWords (in uint16_t)
    // must be >= numChars * scaleX*charW * scaleY*charH.
    bool copyScaledFrom(const PixelFont& src, uint8_t scaleX, uint8_t scaleY,
                        uint16_t* buf, uint32_t bufWords);

    bool            isLoaded() const  { return _pixels != nullptr; }
    uint8_t         charW() const     { return _charW; }
    uint8_t         charH() const     { return _charH; }
    uint16_t        numChars() const  { return _numChars; }
    const char*     charset() const   { return _charset; }
    const uint16_t* glyphPixels(uint16_t idx) const {
        return _pixels + (uint32_t)idx * _charW * _charH;
    }

private:
    uint8_t     _charW    = 0;
    uint8_t     _charH    = 0;
    uint16_t    _numChars = 0;
    const char* _charset  = nullptr;  // pointer to caller-owned string (must outlive PixelFont)
    uint16_t*   _pixels   = nullptr;  // pixel buffer; heap-allocated iff _owned == true
    bool        _owned    = false;
};
