#include "widgets.h"

#include "globals.h"
#include "icons/icons.h"
#include <RetroGfx.h>

extern Display   display;
extern PixelFont wartrisFont2x3;

// No ntp_updated guard on purpose: the clock always shows the internal time,
// even before an NTP sync.
void ClockWidget::render(uint8_t x, uint8_t y) {
    String hhmm = tm.getFormattedTimestamp(dc.clock_24h ? "%H%M" : "%I%M");
    if (hhmm == _lastHhmm) return;
    _lastHhmm = hhmm;
    display.drawText(":", 2, x+13, y, 16, Display::DrawModeBgFill, dc.clock_digits_col);
    display.drawText(hhmm.substring(0, 2).c_str(), 2, x,    y, 16, Display::DrawModeBgFill, dc.clock_digits_col);
    display.drawText(hhmm.substring(2).c_str(),    2, x+19, y, 16, Display::DrawModeBgFill, dc.clock_digits_col);

    if (!dc.clock_24h) {
        // x = SecondsWidget slot x (34 cols); y = below SecondsWidget (slot y=1 + height=12 rows) TODO tune
        String ampm = tm.getFormattedTimestamp("%p");
        display.drawPixelText(wartrisFont2x3, ampm.c_str(), 34*8, 1*8, Display::DrawModeBgFill, dc.clock_digits_col);
    }
}

void SecondsWidget::render(uint8_t x, uint8_t y) {
    int sec = (int)(tm.getLocalTimestamp() % 60);
    if (sec == _lastSec) return;

    if (_lastSec < 0 || sec < _lastSec) {
        display.drawRectangle(dc.hourglass_bg_col, x, y,   4, 1);   // top row: 4 units
        display.drawRectangle(dc.hourglass_bg_col, x, y+1, 5, 11);  // body: 5 units wide
        uint8_t fullRows  = (uint8_t)(sec / 5);
        uint8_t remainder = (uint8_t)(sec % 5);
        // Full rows grow up from the bottom edge…
        if (fullRows > 0)  display.drawRectangle(dc.hourglass_fg_col, x, y + 12 - fullRows, 5, fullRows);
        // …the partial row sits just above them, left-aligned.
        if (remainder > 0) display.drawRectangle(dc.hourglass_fg_col, x, y + 12 - fullRows - 1, remainder, 1);
        _lastSec = sec;
        return;
    }
    _lastSec = sec;
    if (sec > 0) {
        uint8_t adjustedSec = sec - 1;
        display.drawIcon(icon_dot, x + (adjustedSec % 5), y + 11 - (adjustedSec / 5), 0, Display::DrawModeSkip, dc.hourglass_fg_col);
    }
}

void SecondsWidget02::render(uint8_t x, uint8_t y) {
    y+=9;
    int sec = (int)(tm.getLocalTimestamp() % 60);
    if (sec == _lastSec) return;
    _lastSec = sec;
    char buf[3];
    snprintf(buf, sizeof(buf), "%02d", sec);
    // 2x3 glyph = 16x24 px (3 rows). Bottom-align in the 16-row slot (offset 13)
    // so the lower edge matches the clock digits and the hourglass.
    display.drawPixelText(wartrisFont2x3, buf,
                          x * 8, y * 8, Display::DrawModeBgFill, dc.clock_digits_col);
}

void ClockBigWidget::render(uint8_t x, uint8_t y) {
    String hhmm = tm.getFormattedTimestamp(dc.clock_24h ? "%H%M" : "%I%M");
    if (hhmm == _lastHhmm) return;
    _lastHhmm = hhmm;
    if (!dc.clock_24h) {
        x = x - 2;
    }

    display.drawText(":", 0, x+13, y-2, 19, Display::DrawModeBgFill, dc.clock_digits_col);
    display.drawText(hhmm.substring(0, 2).c_str(), 0, x,    y, 19, Display::DrawModeBgFill, dc.clock_digits_col);
    display.drawText(hhmm.substring(2).c_str(),    0, x+19, y, 19, Display::DrawModeBgFill, dc.clock_digits_col);

    if (!dc.clock_24h) {
        // x = right edge of the clock slot (x+33 cols); y = foot of the big clock digits TODO tune
        String ampm = tm.getFormattedTimestamp("%p");
        display.drawPixelText(wartrisFont2x3, ampm.c_str(), (x+33)*8, 16*8, Display::DrawModeBgFill, dc.clock_digits_col);
    }
}



ClockWidget     widget_clock;
ClockBigWidget  widget_clock_big;
SecondsWidget   widget_seconds;
SecondsWidget02 widget_seconds02;
