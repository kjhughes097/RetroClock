#include "eggtimer.h"

#include "globals.h"
#include <RetroGfx.h>
#include <stdio.h>
#include <string.h>

extern Display   display;
extern PixelFont  wartrisFont2x3;

// BTN_*, EggButtonDef, ROW_GX/ROW_GY, kButtons and EGGTIMER_BUTTON_SLOT all live
// in eggtimer.h so main.cpp's Slot rect is derived from the same button table.

namespace {

// Playback is delegated to the global MelodyPlayer 'buzzer' (tune selected by
// index); ALARM_CUTOFF_MS lives in globals.h. LEDC is set up in main.cpp.

// Touch-and-hold auto-repeat for M/S: a short tap increments by 1 on release,
// holding past the threshold auto-repeats, accelerating after HOLD_FAST_AFTER_MS.
constexpr uint32_t HOLD_AUTOREPEAT_THRESHOLD_MS = 2000;
constexpr uint32_t HOLD_FAST_AFTER_MS           = 5000;
constexpr uint32_t HOLD_REPEAT_SLOW_MS          = 200;
constexpr uint32_t HOLD_REPEAT_FAST_MS          = 80;

uint8_t  s_min        = 0;       // 0-99, set value while stopped / paused remainder
uint8_t  s_sec        = 0;       // 0-59
bool     s_running    = false;
uint32_t s_endMillis  = 0;       // millis() target time when running
bool     s_alarmActive  = false;

// Duration of a fresh countdown, kept across pause/resume so STOP can reset to it.
// Only recaptured from a fully-reset state (!s_hasOrig), never on a plain resume.
uint8_t  s_origMin = 0;
uint8_t  s_origSec = 0;
bool     s_hasOrig = false;

// STOP presses since the last Start: 1st = pause, 2nd = reset to started time,
// 3rd = reset to 00:00. Start clears it back to 0.
uint8_t s_stopPressCount = 0;

// Bumped whenever running/alarm state changes, so the button widget knows to
// redraw with updated (enabled/disabled) colors.
uint32_t s_stateGen = 1;

// Touch-hold state for the M/S auto-repeat.
constexpr uint8_t HOLD_NONE = 0xFF;
uint8_t  s_holdBtn       = HOLD_NONE;  // BTN_M / BTN_S index currently pressed, or HOLD_NONE
uint32_t s_holdStartMs   = 0;
uint32_t s_lastRepeatMs  = 0;
bool     s_holdRepeating = false;  // true once the 2 s threshold has been crossed this press

// Set when a touch silences the alarm, so that same still-held touch isn't also
// read as a fresh M/S press by eggTimerHandleHold(). Cleared once the finger lifts.
bool s_suppressHoldUntilRelease = false;

void incrementMinSec(uint8_t idx) {
    if (idx == BTN_M) s_min = (uint8_t)((s_min + 1) % 100);
    else               s_sec = (uint8_t)((s_sec + 1) % 60);
}

void getDisplayValue(uint8_t& m, uint8_t& s) {
    if (!s_running) { m = s_min; s = s_sec; return; }
    int32_t remainMs = (int32_t)(s_endMillis - millis());
    if (remainMs < 0) remainMs = 0;
    // Round up: right after Start, remainMs is a few ms short of the full
    // configured duration flooring would immediately show one second less.
    uint32_t remainSec = ((uint32_t)remainMs + 999) / 1000;
    m = (uint8_t)(remainSec / 60);
    s = (uint8_t)(remainSec % 60);
}

// gx/gy = button's absolute grid position (caller-supplied). bgOverride/txtOverride
// replace the struct defaults unless COLOR_NONE (e.g. to grey out a disabled button).
void drawButton(uint8_t gx, uint8_t gy, const EggButtonDef& def,
                uint8_t bgOverride = Display::COLOR_NONE, uint8_t txtOverride = Display::COLOR_NONE) {
    uint8_t bg = (bgOverride  != Display::COLOR_NONE) ? bgOverride  : def.bgColor;
    uint8_t tc = (txtOverride != Display::COLOR_NONE) ? txtOverride : def.txtColor;

    display.drawRectangle(bg, gx, gy, def.w, def.h);
    int px = (gx + def.textOffX) * Display::BLOCK_PX;
    int py = (gy + def.textOffY) * Display::BLOCK_PX;
    display.drawPixelText(wartrisFont2x3, def.label, px, py, Display::DrawModeSkip, tc);
}

// Rectangle hit-test against each button's actual gx/gy/w/h, so buttons can have
// independent widths/positions/gaps and touch still lines up.
uint8_t hitTestButton(int16_t txPx, int16_t tyPx) {
    for (uint8_t i = 0; i < BTN_COUNT; i++) {
        const EggButtonDef& def = kButtons[i];
        int x0 = (ROW_GX + def.gx) * Display::BLOCK_PX;
        int y0 = (ROW_GY + def.gy) * Display::BLOCK_PX;
        int x1 = x0 + (int)def.w * Display::BLOCK_PX;
        int y1 = y0 + (int)def.h * Display::BLOCK_PX;
        if (txPx >= x0 && txPx < x1 && tyPx >= y0 && tyPx < y1) return i;
    }
    return BTN_COUNT;  // no hit
}

}  // namespace

// === EggTimerDisplayWidget (big MM:SS readout) ===

class EggTimerDisplayWidget : public Widget {
    char _lastStr[6] = "";
    bool _colonDrawn = false;
public:
    const char* name() const override { return "eggtimer-display"; }

    void onActivate(int16_t, int16_t) override {
        _lastStr[0]  = '\0';  // force full repaint
        _colonDrawn  = false;
    }

    void render(uint8_t x, uint8_t y) override {
        uint8_t m, s;
        getDisplayValue(m, s);
        char buf[6];
        snprintf(buf, sizeof(buf), "%02u%02u", m, s);
        if (strcmp(buf, _lastStr) == 0) return;
        strcpy(_lastStr, buf);

        // The colon is static drawing it every refresh (BgFill mode clears the
        // glyph cell first) caused a visible flicker once per second. Draw once.
        if (!_colonDrawn) {
            display.drawText(":", 0, x + 13, y - 2, 19, Display::DrawModeBgFill, dc.clock_digits_col);
            _colonDrawn = true;
        }
        char part[3];
        snprintf(part, sizeof(part), "%.2s", buf);
        display.drawText(part, 0, x, y, 19, Display::DrawModeBgFill, dc.clock_digits_col);
        snprintf(part, sizeof(part), "%.2s", buf + 2);
        display.drawText(part, 0, x + 19, y, 19, Display::DrawModeBgFill, dc.clock_digits_col);
    }
};

// === EggTimerButtonsWidget (single widget: M | S | START | STOP row) ===
// One widget for the whole button row (hitTestButton maps touch → button index),
// instead of 4 separate widgets/slots.

class EggTimerButtonsWidget : public Widget {
    uint32_t _lastStateGen = 0;
public:
    const char* name() const override { return "eggtimer-buttons"; }

    void onActivate(int16_t txPx, int16_t tyPx) override {
        if (txPx < 0) {
            _lastStateGen = s_stateGen - 1;  // screen-entry sweep: force a repaint, not a real tap
            return;
        }

        uint8_t idx = hitTestButton(txPx, tyPx);
        if (idx >= BTN_COUNT) return;

        switch (idx) {
            case BTN_M:
            case BTN_S:
                // Handled entirely by eggTimerHandleHold() based on press/hold/release
                // timing (tap-on-release, or auto-repeat once held past the threshold).
                break;
            case BTN_START:
                if (!s_running && (s_min > 0 || s_sec > 0)) {
                    // Only capture "orig" when starting from a fully-reset
                    // state resuming after a pause must keep the original
                    // started duration, not the already-decremented value.
                    if (!s_hasOrig) {
                        s_origMin = s_min;
                        s_origSec = s_sec;
                        s_hasOrig = true;
                    }
                    s_stopPressCount = 0;
                    s_endMillis      = millis() + ((uint32_t)s_min * 60 + s_sec) * 1000UL;
                    s_running        = true;
                    s_stateGen++;
                }
                break;
            case BTN_STOP:
                if (s_running) {
                    // 1st press: pause, freeze the remaining time. Round up
                    // the same way getDisplayValue() does, so the frozen
                    // value matches what was just shown on screen.
                    int32_t remainMs = (int32_t)(s_endMillis - millis());
                    if (remainMs < 0) remainMs = 0;
                    uint32_t remainSec = ((uint32_t)remainMs + 999) / 1000;
                    s_min            = (uint8_t)(remainSec / 60);
                    s_sec            = (uint8_t)(remainSec % 60);
                    s_running        = false;
                    s_stopPressCount = 1;
                    s_stateGen++;
                    break;
                }
                if (!s_hasOrig) {
                    // Never started this session go straight to 00:00.
                    s_min = s_sec = 0;
                    s_stopPressCount = 0;
                    break;
                }
                s_stopPressCount++;
                if (s_stopPressCount == 2) {
                    // 2nd press: reset to the originally started time.
                    s_min = s_origMin;
                    s_sec = s_origSec;
                } else {
                    // 3rd press: reset to 00:00, ready for a new session.
                    s_min            = 0;
                    s_sec            = 0;
                    s_hasOrig        = false;
                    s_stopPressCount = 0;
                }
                break;
        }
    }

    void render(uint8_t x, uint8_t y) override {
        if (_lastStateGen == s_stateGen) return;
        _lastStateGen = s_stateGen;

        for (uint8_t i = 0; i < BTN_COUNT; i++) {
            // Grey out M/S/START while running, STOP while stopped every
            // other case keeps the button's own default color from kButtons.
            uint8_t bgOverride = Display::COLOR_NONE;
            if (i == BTN_STOP) {
                if (!s_running) bgOverride = grey1;
            } else if (s_running) {
                bgOverride = grey1;
            }
            drawButton((uint8_t)(x + kButtons[i].gx), (uint8_t)(y + kButtons[i].gy),
                       kButtons[i], bgOverride);
        }
    }
};

// === Public instances ===

static EggTimerDisplayWidget _widget_display;
static EggTimerButtonsWidget _widget_buttons;

Widget& widget_eggtimer_display = _widget_display;
Widget& widget_eggtimer_buttons = _widget_buttons;

// === Public functions ===

void eggTimerInit() {
    s_min            = s_sec = 0;
    s_running        = false;
    s_alarmActive    = false;
    s_hasOrig        = false;
    s_stopPressCount = 0;
    // LEDC for the buzzer is configured once via buzzer.begin() in main.cpp.
}

void eggTimerTick() {
    if (s_running) {
        if ((int32_t)(s_endMillis - millis()) <= 0) {
            s_running      = false;
            s_min = s_sec  = 0;
            s_alarmActive  = true;
            uint16_t sel   = dc.alarm_sound;          // 1..N from WebPrefs
            if (sel < 1 || sel > buzzer.count()) sel = 1;
            buzzer.play(sel - 1, dc.volume, ALARM_CUTOFF_MS);
            s_stateGen++;
            display.displayOn();       // wake the screen if it was manually off
        }
    }

    // The buzzer owns playback and the safety cutoff; once it has stopped (touch
    // or its own max-duration) clear the egg-timer's alarm state and restore the
    // configured duration.
    if (s_alarmActive && !buzzer.isPlaying()) {
        eggTimerSilenceAlarm();
    }
}

bool eggTimerAlarmActive() {
    return s_alarmActive;
}

void eggTimerSilenceAlarm() {
    buzzer.stop();                     // silence + release pin (no buzz, no heap)
    s_alarmActive = false;
    // Restore the originally started duration, ready to run again unchanged.
    if (s_hasOrig) {
        s_min = s_origMin;
        s_sec = s_origSec;
    }
    s_stopPressCount          = 0;
    s_suppressHoldUntilRelease = true;
    s_stateGen++;
}

void eggTimerHandleHold(bool isActiveScreen, bool touched, int16_t txPx, int16_t tyPx) {
    if (s_suppressHoldUntilRelease) {
        if (!touched) s_suppressHoldUntilRelease = false;
        s_holdBtn       = HOLD_NONE;
        s_holdRepeating = false;
        return;
    }

    // Only M/S; only while stopped and on this screen.
    uint8_t idx = HOLD_NONE;
    if (touched && isActiveScreen && !s_running) {
        uint8_t hit = hitTestButton(txPx, tyPx);
        if (hit == BTN_M || hit == BTN_S) idx = hit;
    }

    if (idx == HOLD_NONE) {
        // Not currently pressing M/S. If we were tracking a press, this is its
        // release: a short tap (never crossed the auto-repeat threshold) fires
        // its single +1 here, on release.
        if (s_holdBtn != HOLD_NONE && !s_holdRepeating) {
            incrementMinSec(s_holdBtn);
        }
        s_holdBtn       = HOLD_NONE;
        s_holdRepeating = false;
        return;
    }

    uint32_t now = millis();
    if (s_holdBtn != idx) {
        // Fresh press starting no increment yet, wait for release or the
        // auto-repeat threshold.
        s_holdBtn       = idx;
        s_holdStartMs   = now;
        s_holdRepeating = false;
        return;
    }

    uint32_t heldFor = now - s_holdStartMs;
    if (heldFor < HOLD_AUTOREPEAT_THRESHOLD_MS) return;  // still within tap-on-release window

    if (!s_holdRepeating) {
        // Crossing the threshold while still held start auto-repeat immediately.
        s_holdRepeating = true;
        s_lastRepeatMs  = now;
        incrementMinSec(idx);
        return;
    }

    uint32_t interval = (heldFor >= HOLD_FAST_AFTER_MS) ? HOLD_REPEAT_FAST_MS : HOLD_REPEAT_SLOW_MS;
    if (now - s_lastRepeatMs < interval) return;
    s_lastRepeatMs = now;
    incrementMinSec(idx);
}
