#ifndef ALARMCLOCK_H
#define ALARMCLOCK_H

// Scheduled alarm clock configured only via WebPrefs (no screen, no snooze).
// Checked once per minute against the local time: when the configured HH:MM
// matches on an enabled weekday it starts the shared 'buzzer'. A touch silences
// it (handled in main's touch path); the buzzer also auto-stops after its
// safety cutoff. An all-zero weekday mask disables it.
void tickAlarmClock();

#endif // ALARMCLOCK_H
