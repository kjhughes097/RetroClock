#pragma once

#include <stdint.h>

// Non-blocking buzzer melody player for a passive piezo on an ESP32 LEDC channel.
// Uses LEDC directly, not Arduino tone() (which spawns a ~6.5 KB FreeRTOS task);
// the pin is attached only while sounding. One channel = one melody at a time;
// play() is last-wins. Call tick() once per loop(), regardless of the source.
class MelodyPlayer {
public:
    static constexpr uint8_t NO_PIN = 0xFF;

    // Configure the LEDC channel once (register-only, no heap); the pin attaches
    // in play(). Keep off ch 7 (CYD backlight); default 0.
    // 'ampEnablePin' drives an audio amp's active-low SHUTDOWN input; it is pulled
    // low here and stays there, so the amp behaves like one that is hard-wired on.
    void begin(uint8_t gpioPin, uint8_t ledcChannel = 0, uint8_t ampEnablePin = NO_PIN);

    // Start built-in melody #index (clamped), looping until stop(). masterVolPct
    // (0..100) scales the per-song volume; maxMs is a safety auto-stop (0 = none).
    // Stops any current melody first (last-wins).
    void play(uint8_t index, uint8_t masterVolPct = 100, uint32_t maxMs = 0);

    // Silence immediately and release the pin (floating/idle, no buzz, no heap).
    // Only call from the loop()/tick() task the LEDC + state mutation is not
    // thread-safe. From another task (e.g. a web-server callback) use requestStop().
    void stop();

    // Thread-safe stop request: only sets a flag; the actual stop() runs inside
    // the next tick() (on the loop task). Use this from any task other than loop().
    void requestStop() { _stopReq = true; }

    // Drive the note state machine + the safety cutoff. Call every loop().
    void tick();

    bool    isPlaying() const { return _active; }
    uint8_t count() const;                              // number of built-in melodies

private:
    uint8_t  _pin       = 0;
    uint8_t  _ch        = 0;
    bool     _active    = false;
    uint32_t _startMs   = 0;
    uint32_t _maxMs     = 0;     // safety cutoff for the active melody (0 = none)
    uint8_t  _idx       = 0;     // index into the melody bank of the active tune
    uint16_t _noteIdx   = 0;     // next note to play within the melody
    uint32_t _noteEndMs = 0;     // millis() when the current phase ends
    bool     _sounding  = false; // true = tone playing, false = gap/rest
    uint16_t _duty      = 512;   // LEDC duty for the active tune (volume; 512 = loudest)
    volatile bool _stopReq = false; // cross-task stop request, consumed in tick()
};
