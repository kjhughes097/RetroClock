#pragma once

#include <stdint.h>

struct Font {
    uint8_t        charW;
    uint8_t        charH;
    uint8_t        numChars;
    const char*    charset;  // null-terminated lookup string, length == numChars
    const uint8_t* glyphs;   // numChars * charW * charH bytes, row-major per glyph
};
