#include "globals.h"
#include "alarmclock.h"
#include "eggtimer.h"
#include <RetroGfx.h>

extern Display display;

// WebPrefs field validators run in setValue before the value is stored; returning
// false makes WebPrefs keep the previous value, so a typo can never store garbage.

// Strict "HH:MM": exactly 5 chars, digits with a colon at index 2, 00:00..23:59.
bool isValidTime(const char *s) {
    if (!s || strlen(s) != 5 || s[2] != ':') return false;
    if (s[0] < '0' || s[0] > '9' || s[1] < '0' || s[1] > '9' ||
        s[3] < '0' || s[3] > '9' || s[4] < '0' || s[4] > '9') return false;
    int h = (s[0] - '0') * 10 + (s[1] - '0');
    int m = (s[3] - '0') * 10 + (s[4] - '0');
    return h <= 23 && m <= 59;
}

// Weekday mask: exactly 7 characters, each '0' or '1' (e.g. "1100000").
bool isValidDays(const char *s) {
    if (!s || strlen(s) != 7) return false;
    for (int i = 0; i < 7; i++) if (s[i] != '0' && s[i] != '1') return false;
    return true;
}

// WebPrefs "Alarm State" switch (mirrors buzzer.isPlaying() via cbBeforeResponse).
// Turning it off silences the shared buzzer, whichever source is sounding.
void cbAlarmSwitch() {
    // Runs on the async web-server task -> defer the actual stop to the loop task
    // (buzzer.tick()) so LEDC + player state are never mutated cross-core.
    if (!dc.alarm_state) {
        buzzer.requestStop();
        eggTimerRequestStopFlash();
    }
}

void tickAlarmClock() {
    static int s_lastFiredMin = -1;   // matched minute already handled (fire-once guard)

    // Need a real clock: stored_timestamp stays 0 until NTP sync or a manual set,
    // so this also prevents a spurious fire right after boot.
    if (tm.getTimestamp() < 1700000000UL) { s_lastFiredMin = -1; return; }

    int setMin = parseHHMM(dc.alarm_time);
    if (setMin < 0) return;

    // Mask is Mon..Sun (index 0..6); tm.getWeekday() is 0=Sun..6=Sat.
    int  monIdx = (tm.getWeekday() + 6) % 7;
    bool dayOn  = (dc.alarm_days[monIdx] == '1');   // all-zero mask = disabled
    int  curMin = tm.getHour() * 60 + tm.getMinute();

    if (dayOn && curMin == setMin) {
        if (s_lastFiredMin != curMin) {          // fire exactly once this minute
            s_lastFiredMin = curMin;
            uint16_t sel = dc.alarm_sound;       // shared alarm sound/volume
            if (sel < 1 || sel > buzzer.count()) sel = 1;
            buzzer.play(sel - 1, dc.volume, ALARM_CUTOFF_MS);  // ignored if busy
            display.displayOn();                 // wake the screen
            LOG_SINFO("[alarmclock] fired at %s", dc.alarm_time);
        }
    } else if (curMin != setMin) {
        s_lastFiredMin = -1;                     // re-arm once the minute passes
    }
}
