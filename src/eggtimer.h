#ifndef EGGTIMER_H
#define EGGTIMER_H

#include "screens.h"
#include <RetroGfx.h>   // Palette color constants (blue1, grey3, …) used by kButtons

// Egg-timer / countdown screen: big MM:SS readout + M / S / START / STOP buttons.
// M and S only take effect while stopped; tapping M/S while running is ignored.
// A short tap on M/S increments by 1 on release; holding past ~2 s switches to
// auto-repeat (accelerating) while held, so you don't need 100 taps to reach 99.
// At 00:00 the speaker (SPEAKER_PIN) plays Beethoven's "Für Elise" on a loop
// until the screen is touched, or for a maximum of 5 minutes if never touched.
// It also flashes the whole screen (panel color inversion), whatever screen is
// shown: for dc.timer_flash_sec seconds (0 = off), or until touched when
// dc.timer_flash_hold is on. A touch dismisses sound and flash together.

// === Button row layout single source of truth ===
// The geometry below is shared by THREE consumers so they can never drift apart:
//   1. drawButton()    (eggtimer.cpp) where each button is drawn
//   2. hitTestButton() (eggtimer.cpp) which button a touch landed on
//   3. EGGTIMER_BUTTON_SLOT (below)   the Slot rect main.cpp uses to gate
//      whether a touch is dispatched to the widget at all
// Because the Slot rect is computed from the same kButtons table, a tap that
// hitTestButton() accepts is guaranteed to pass the Slot gate too.

enum { BTN_M = 0, BTN_S = 1, BTN_START = 2, BTN_STOP = 3, BTN_COUNT = 4 };

// gx/gy are relative to the row origin (ROW_GX/ROW_GY); all sizes in grid cells.
struct EggButtonDef {
    const char* label;
    uint8_t     bgColor;   // default background color
    uint8_t     txtColor;  // default text color
    uint8_t     w;         // background width, grid cells
    uint8_t     h;         // background height, grid cells
    uint8_t     textOffX;  // text x offset from the background's origin, grid cells
    uint8_t     textOffY;  // text y offset from the background's origin, grid cells
    uint8_t     gx;        // x position, grid cells, relative to the row origin
    uint8_t     gy;        // y position, grid cells, relative to the row origin
};

constexpr uint8_t ROW_GX = 0;
constexpr uint8_t ROW_GY = 20;

// textOffX/Y approximate centering for each button's own w/h retune if those
// change. wartrisFont2x3 glyphs are 16x24 px (2x3 grid cells).
inline constexpr EggButtonDef kButtons[BTN_COUNT] = {
    { "MIN",   blue1,  grey3, 8,  9, 1, 3, 1,  0 },
    { "SEC",   blue2,  grey3, 8,  9, 1, 3, 9,  0 },
    { "START", green1, grey3, 12, 9, 1, 3, 17, 0 },
    { "STOP",  red1,   grey3, 10, 9, 1, 3, 29, 0 },
};

constexpr Rect eggButtonRowSlot() {
    //uint8_t minX = 0xFF, minY = 0xFF, maxX = 0, maxY = 0;
    uint8_t w = 0, h = 0;
    for (const EggButtonDef& b : kButtons) {
        if ((uint8_t)(b.gx + b.w) > w) w = (uint8_t)(b.gx + b.w);
        if ((uint8_t)(b.gy + b.h) > h) h = (uint8_t)(b.gy + b.h);    
    }
    return Rect{ ROW_GX, ROW_GY, w, h };
}
inline constexpr Rect EGGTIMER_BUTTON_SLOT = eggButtonRowSlot();

// === Public API ===

void eggTimerInit();

// Call every loop() iteration drives the countdown and the alarm beep.
void eggTimerTick();

// Call every loop() iteration (regardless of touch edge) drives the M/S
// tap-on-release / hold-to-auto-repeat state machine. isActiveScreen must be
// true only while the egg-timer screen is the one currently shown.
void eggTimerHandleHold(bool isActiveScreen, bool touched, int16_t txPx, int16_t tyPx);

// True while the 00:00 alarm is sounding or the screen is flashing. main.cpp
// checks this before dispatching a touch to screensHandleTouch() so any tap
// dismisses the alarm instead of triggering a button/screen-switch underneath it.
bool eggTimerAlarmActive();
void eggTimerSilenceAlarm();  // stops sound + flash (loop task only)

// Thread-safe: asks the loop task to stop the flash on its next tick (used by
// the WebPrefs "Alarm Active" switch, which runs on the async web-server task).
void eggTimerRequestStopFlash();

extern Widget& widget_eggtimer_display;
extern Widget& widget_eggtimer_buttons;  // single full-width row: M | S | START | STOP

#endif  // EGGTIMER_H
