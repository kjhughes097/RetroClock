#pragma once

#include <stdint.h>

struct Icon {
    uint8_t        width;
    uint8_t        height;
    const uint8_t* data;  // width*height bytes, row-major, 0..31 = palette, 32 = transparent
};

struct IconSet {
    const Icon* icons;
    uint8_t     count;
};
