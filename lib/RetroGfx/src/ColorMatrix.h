#pragma once

#include <stdint.h>
#include "serlog.h"

class ColorMatrix {
public:
    static constexpr uint8_t SIZE        = 32;     // 32 tiles
    static constexpr uint8_t BLOCK_PX    = 8;      // each tile is 8x8 px
    static constexpr uint8_t TILE_PIXELS = BLOCK_PX * BLOCK_PX;
    static constexpr uint8_t TRANSPARENT = 32;

    bool            loadFromFile(const char* path);
    const uint16_t* tilePixels(uint8_t idx) const { return _tiles[idx & 0x1F]; }
    // Single representative RGB565 of a tile (center pixel) for solid-color rendering paths.
    uint16_t        soloColor(uint8_t idx) const { return _tiles[idx & 0x1F][3 * BLOCK_PX + 3]; }
    bool            isLoaded() const { return _loaded; }

private:
    uint16_t _tiles[SIZE][TILE_PIXELS] = {{0}};
    bool     _loaded                   = false;
};
