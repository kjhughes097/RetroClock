#ifndef HARDWARE_H
#define HARDWARE_H

// NOTE: the CYD display + touch pins are NOT here they live in the LovyanGFX
// config in lib/RetroGfx/src/Display.cpp. This file only holds the few pins and
// build-time constants the application itself references.

// Passive buzzer for the egg-timer alarm.
#define SPEAKER_PIN 26

// SHUTDOWN input (active low) of the on-board audio amp on the ILI9341 CYD
// revision; left floating there, which mutes the amp. Unconnected on the ST7789
// revision, where the amp is hard-wired on, so it is not driven there.
#define AMP_ENABLE_PIN 4

#endif // HARDWARE_H
