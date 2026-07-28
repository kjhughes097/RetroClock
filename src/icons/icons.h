#pragma once

#include <RetroGfx.h>

namespace retroclock_icons_internal {
inline constexpr uint8_t T = 32;  // transparent

inline constexpr uint8_t sun1_data[] = {
    T, T, T,23, T, T, T,
    T,23, T, T, T,23, T,
    T, T,23,23,23, T, T,
    23,T,23,23,23, T,23,
    T, T,23,23,23, T, T,
    T,23, T, T, T,23, T,
    T, T, T,23, T, T, T
};

inline constexpr uint8_t sun2_data[] = {
    T, T, T,23, T, T, T,
    T,23, T, T, T,23, T,
    T, T,23,23,23, T, T,
    23,T,23, 1, 2, 2, 0,
    T, T, 1, 2, 2, 2, 2,
    T,23, 1, 2, 2, 2, 2,
    T, T, T, 1, 2, 2, 0,
};

inline constexpr uint8_t sun3_data[] = {
    T, T, T,23, T, T, T,
    T,23, T, T, T,23, T,
    T, T,23,23,23, T, T,
    23,T,23, 1, 2, 2, 0,
    T, T, 1, 2, 2, 2, 2,
    T,23, 1,11, 2,11, 2,
    T, T, T, 1,11, 2,11,
};

inline constexpr uint8_t cloud1_data[] = {
    T, T, T, T, T, T, T,
    T, 1, 1, 1, T, T, T,
    1, 1, 1, 2, 2, 2, T,
    1, 1, 2, 2, 2, 2, 2,
    T, 1, 2, 2, 2, 2, 2,
    T, T, T, 2, 2, 2, T,
    T, T, T, T, T, T, T,    
};

inline constexpr uint8_t fog_data[] = {
    1, 2, 1, 2, 1, 2, 1,
    2, 1, 2, 1, 2, 1, 2,
    1, 2, 1, 2, 1, 2, 1,
    2, 1, 2, 1, 2, 1, 2,
    1, 2, 1, 2, 1, 2, 1,
    2, 1, 2, 1, 2, 1, 2,
    1, 2, 1, 2, 1, 2, 1,

};

inline constexpr uint8_t rain_data[] = {
    T, 1, 1, 1, T, T, T,
    1, 1, 1, 2, 2, 2, T,
    1, 1, 2, 2, 2, 2, 2,
    T, 1, 2, 2, 2, 2, 2,
    T,11, T, 2, 2, 2, T,
    T, T, T, T, T, T, T,
    T,11, T,11, T,11, T,
};

inline constexpr uint8_t storm_data[] = {
    T, 2, 2, 2, T, T, T,
    2, 2, 2, 1, 1, 1, T,
    2, 2, 1, 1, 1, 1, 1,
    T, 2, 1, 1,23, 1, 1,
    T,11, T, 1, 1,23, T,
    T, T, T, T,23, T, T,
    T,11, T,11, T,23, T,
};

inline constexpr uint8_t snow_data[] = {
    T, 1, 1, 1, T, T, T,
    1, 1, 1, 2, 2, 2, T,
    1, 1, 2, 2, 2, 2, 2,
    T, 1, 2, 3, 2, 2, 2,
    3, T, T, 2, 2, 2, 3,
    T, T, 3, T, T, T, T,
    3, T, T, T, 3, T, 3,
};

inline constexpr uint8_t night_data[] = {
    T, T, T, T, T, T, T,
    T,23, T, T,21, T, T,
    T, T, T,21,23,21, T,
    T, T, T, T,21, T, T,
    T, T, T, T, T, T,22,
    T, T,22, T, T, T, T,
    T, T, T, T, T, T,23,
};

// 1x1 dot useful as a single 8x8 marker; combine with colorOverride to colour it.
inline constexpr uint8_t dot_data[] = {
    0,
};

inline constexpr Icon icons[] = {
    { 1, 1, dot_data    },  // [0] icon_dot

    { 7, 7, sun1_data   },  // [1] weather_sun
    { 7, 7, sun2_data   },  // [2] weather_sun_cloud
    { 7, 7, sun3_data   },  // [3] weather_sun_cloud_rain
    { 7, 7, cloud1_data },  // [4] weather_cloud
    { 7, 7, fog_data    },  // [5] weather_fog
    { 7, 7, rain_data   },  // [6] weather_rain
    { 7, 7, storm_data  },  // [7] weather_storm
    { 7, 7, snow_data   },  // [8] weather_snow
    { 7, 7, night_data  },  // [9] weather_night
};
}  // namespace retroclock_icons_internal

// Named indices into projectIcons. Anonymous enum auto-numbers; the static_assert
// below catches any mismatch with the array length.
enum : uint8_t {
    icon_dot = 0,
    weather_sun,
    weather_sun_cloud,
    weather_sun_cloud_rain,
    weather_cloud,
    weather_fog,
    weather_rain,
    weather_storm,
    weather_snow,
    weather_night,
    icon_count,
};

static_assert(
    sizeof(retroclock_icons_internal::icons) / sizeof(retroclock_icons_internal::icons[0])
        == icon_count,
    "icons[] length and icon_count enum are out of sync");

inline constexpr IconSet projectIcons = {
    retroclock_icons_internal::icons,
    sizeof(retroclock_icons_internal::icons) / sizeof(retroclock_icons_internal::icons[0]),
};
