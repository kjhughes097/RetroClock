#ifndef WEATHER_H
#define WEATHER_H

#include "screens.h"

// 5-hour weather forecast widget. Data fetched from open-meteo.com.
// Slots cover the current local hour + next 4 hours.

void weatherInit();

extern Widget& widget_weather;
extern Widget& widget_weather2;
extern Widget& widget_temp_current;

#endif  // WEATHER_H
