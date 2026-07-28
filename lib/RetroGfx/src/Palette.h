#pragma once

#include <stdint.h>

// Named indices into the 32-color ColorMatrix palette.
// Layout is 8 hue groups × 4 brightness levels (0 = darkest, 3 = brightest).

inline constexpr uint8_t grey0   = 0;
inline constexpr uint8_t grey1   = 1;
inline constexpr uint8_t grey2   = 2;
inline constexpr uint8_t grey3   = 3;

inline constexpr uint8_t violet0 = 4;
inline constexpr uint8_t violet1 = 5;
inline constexpr uint8_t violet2 = 6;
inline constexpr uint8_t violet3 = 7;

inline constexpr uint8_t blue0   = 8;
inline constexpr uint8_t blue1   = 9;
inline constexpr uint8_t blue2   = 10;
inline constexpr uint8_t blue3   = 11;

inline constexpr uint8_t cyan0   = 12;
inline constexpr uint8_t cyan1   = 13;
inline constexpr uint8_t cyan2   = 14;
inline constexpr uint8_t cyan3   = 15;

inline constexpr uint8_t green0  = 16;
inline constexpr uint8_t green1  = 17;
inline constexpr uint8_t green2  = 18;
inline constexpr uint8_t green3  = 19;

inline constexpr uint8_t yellow0 = 20;
inline constexpr uint8_t yellow1 = 21;
inline constexpr uint8_t yellow2 = 22;
inline constexpr uint8_t yellow3 = 23;

inline constexpr uint8_t orange0 = 24;
inline constexpr uint8_t orange1 = 25;
inline constexpr uint8_t orange2 = 26;
inline constexpr uint8_t orange3 = 27;

inline constexpr uint8_t red0    = 28;
inline constexpr uint8_t red1    = 29;
inline constexpr uint8_t red2    = 30;
inline constexpr uint8_t red3    = 31;
