#ifndef WIDGETS_H
#define WIDGETS_H

#include <Arduino.h>
#include "screens.h"

class ClockWidget : public Widget {
    String _lastHhmm;
public:
    const char* name() const override { return "clock"; }
    void render(uint8_t x, uint8_t y) override;
    void onActivate(int16_t, int16_t) override { _lastHhmm = ""; }
};

class ClockBigWidget : public Widget {
    String _lastHhmm;
public:
    const char* name() const override { return "clock-big"; }
    void render(uint8_t x, uint8_t y) override;
    void onActivate(int16_t, int16_t) override { _lastHhmm = ""; }
};

class SecondsWidget : public Widget {
    int _lastSec = -1;
public:
    const char* name() const override { return "seconds"; }
    void render(uint8_t x, uint8_t y) override;
    void onActivate(int16_t, int16_t) override { _lastSec = -1; }
};

// Alternative seconds widget: shows the seconds as a 2-digit number in the 2x3
// font, in the same slot/position as SecondsWidget (hourglass). No background
// fill the screen switch already clears the area.
class SecondsWidget02 : public Widget {
    int _lastSec = -1;
public:
    const char* name() const override { return "seconds02"; }
    void render(uint8_t x, uint8_t y) override;
    void onActivate(int16_t, int16_t) override { _lastSec = -1; }
};

extern ClockWidget     widget_clock;
extern ClockBigWidget  widget_clock_big;
extern SecondsWidget   widget_seconds;
extern SecondsWidget02 widget_seconds02;

#endif  // WIDGETS_H
