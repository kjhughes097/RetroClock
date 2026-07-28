#include "MelodyPlayer.h"

#include <Arduino.h>

namespace {

// Note frequencies in Hz (n-prefixed to avoid clashing with Arduino pin macros
// like A4). nREST = silence.
constexpr uint16_t nREST = 0;
constexpr uint16_t nC4 = 262, nD4 = 294, nE4 = 330, nF4 = 349, nG4 = 392,
                   nGS4 = 415, nA4 = 440, nB4 = 494, nC5 = 523, nD5 = 587,
                   nDS5 = 622, nE5 = 659, nF5 = 698, nG5 = 784, nA5 = 880,
                   nAS5 = 932, nB5 = 988, nC6 = 1047, nCS6 = 1109, nD6 = 1175,
                   nDS6 = 1245, nF6 = 1397, nFS6 = 1480, nG6 = 1568, nGS6 = 1661,
                   nA6 = 1760, nAS6 = 1865, nB6 = 1976, nC7 = 2093;

// One melody step: a frequency (0 = rest) and a note-length code:
//   4 = quarter, 8 = eighth, 16 = sixteenth; negative = dotted (1.5x).
struct Note { uint16_t freq; int8_t div; };

constexpr uint16_t NOTE_GAP_MS       = 12;    // tiny silence between notes (articulation)
constexpr uint32_t MELODY_REPEAT_GAP = 1000;  // pause before the tune loops again

// Milliseconds for a note-length code at a given tempo (negative = dotted = 1.5x).
constexpr uint32_t noteMs(int8_t div, uint16_t bpm) {
    uint32_t whole = (60000UL * 4) / bpm;   // whole-note duration in ms
    return (div > 0) ? (whole / (uint8_t)div)
                     : (whole / (uint8_t)(-div)) * 3 / 2;
}

// "Ring High" plain phone-style alarm ring (RTTTL o=5,d=16,b=350).
constexpr Note kRingHigh[] = {
    {nB5,16},{nD5,16},{nB5,16},{nD5,16},{nB5,16},{nD5,16},{nB5,16},{nD5,16},
    {nD5,16},{nF5,16},{nD5,16},{nF5,16},{nD5,16},{nF5,16},{nD5,16},{nF5,16},
    {nF5,16},{nA5,16},{nF5,16},{nA5,16},{nF5,16},{nA5,16},{nF5,16},{nA5,-16},
};
constexpr uint16_t RING_HIGH_LEN = sizeof(kRingHigh) / sizeof(kRingHigh[0]);

// "Yankee Doodle"
constexpr Note kYankeeDoodle[] = {
    {nFS6,8},{nFS6,8},{nGS6,8},{nAS6,8},{nFS6,8},{nAS6,8},{nGS6,8},{nCS6,8},
    {nFS6,8},{nFS6,8},{nGS6,8},{nAS6,8},{nFS6,4},
    {nF6,8},{nCS6,8},{nFS6,8},{nFS6,8},{nGS6,8},{nAS6,8},{nB6,8},{nAS6,8},{nGS6,8},{nFS6,8},
    {nF6,8},{nCS6,8},{nDS6,8},{nF6,8},{nFS6,4},
    {nFS6,4},{nDS6,8},{nF6,8},{nDS6,8},{nCS6,8},{nDS6,8},{nF6,8},{nFS6,4},
    {nCS6,8},{nDS6,8},{nCS6,8},{nB5,8},{nAS5,4},{nCS6,4},
    {nDS6,8},{nF6,8},{nDS6,8},{nCS6,8},{nDS6,8},{nF6,8},
    {nFS6,8},{nDS6,8},{nCS6,8},{nFS6,8},{nF6,8},{nGS6,8},{nFS6,4},{nFS6,-8},
};
constexpr uint16_t YANKEE_DOODLE_LEN = sizeof(kYankeeDoodle) / sizeof(kYankeeDoodle[0]);

// "1942"
constexpr Note k1942[] = {
    {nC5,16},{nREST,16},{nC5,16},{nREST,16},{nC5,16},{nREST,16},{nF5,16},{nREST,16},
    {nG5,16},{nREST,16},{nAS5,16},{nREST,16},{nA5,-8},{nREST,16},{nC6,-1},{nREST,4},
    {nC5,16},{nREST,16},{nC5,16},{nREST,16},{nC5,16},{nREST,16},{nF5,16},{nREST,16},
    {nG5,16},{nREST,16},{nAS5,16},{nREST,16},{nA5,-1},{nREST,4},
    {nC5,16},{nREST,16},{nC5,16},{nREST,16},{nC5,16},{nREST,16},{nF5,16},{nREST,16},
    {nG5,16},{nREST,16},{nAS5,16},{nREST,16},{nA5,-8},{nREST,16},
    {nF5,8},{nREST,8},{nG5,16},{nREST,16},{nAS5,16},{nREST,16},{nA5,-8},{nREST,16},
    {nF5,8},{nREST,8},{nG5,16},{nREST,16},{nAS5,16},{nREST,16},{nA5,-8},{nREST,16},
    {nF5,8},{nREST,8},{nA5,16},{nREST,16},{nC6,16},{nREST,16},{nD6,1},{nREST,2},
};
constexpr uint16_t K1942_LEN = sizeof(k1942) / sizeof(k1942[0]);

// "Sanxion"
constexpr Note kSanxion[] = {
    {nG6,8},{nG6,8},{nD6,8},{nREST,8},{nF6,8},{nREST,8},{nC6,8},{nD6,8},{nREST,8},
    {nAS5,8},{nREST,8},{nF5,8},{nG5,8},{nREST,8},{nF6,8},{nG6,8},
    {nAS5,8},{nAS5,8},{nC6,8},{nREST,8},{nA5,8},{nREST,8},{nC6,8},{nF6,8},
    {nAS6,8},{nAS6,8},{nA6,8},{nREST,8},{nF6,8},{nREST,8},
    {nC6,8},{nC6,8},{nAS5,8},{nAS5,8},{nC6,8},{nREST,8},{nAS6,8},{nREST,8},
    {nC6,8},{nAS6,8},{nC7,8},{nAS6,8},{nC6,8},{nREST,8},{nAS5,8},{nREST,8},{nF5,8},{nREST,8},
    {nG5,1},{nREST,1},
};
constexpr uint16_t SANXION_LEN = sizeof(kSanxion) / sizeof(kSanxion[0]);

// "lazy2" converted 1:1 from RTTTL (d=4,o=5,b=140).
constexpr Note kLazy2[] = {
    {nAS5,8},{nCS6,8},{nDS6,8},{nF6,8},{nAS5,8},{nREST,2},
    {nAS5,8},{nCS6,8},{nDS6,8},{nF6,8},{nFS6,8},{nF6,8},{nCS6,8},{nDS6,-8},
    {nREST,4},{nCS6,8},{nREST,8},{nF6,8},{nAS5,8},{nREST,1},
};
constexpr uint16_t LAZY2_LEN = sizeof(kLazy2) / sizeof(kLazy2[0]);

// vol = per-song volume percent (0..100) -> LEDC duty; 50 % duty (128) is loudest,
// 0 % silent. Tune each song's loudness here.
struct Melody { const Note* notes; uint16_t len; uint16_t bpm; uint8_t vol; };

// Bank order defines the public index (and maps to dc.alarm_sound 1..N):
//   0=Ring High, 1=Yankee, 2=1942, 3=Sanxion, 4=Lazy.
constexpr Melody kBank[] = {
    { kRingHigh,     RING_HIGH_LEN,     350, 100 },
    { kYankeeDoodle, YANKEE_DOODLE_LEN, 120, 100 },
    { k1942,         K1942_LEN,         200, 100 },
    { kSanxion,      SANXION_LEN,       245, 100 },
    { kLazy2,        LAZY2_LEN,         140,  70 },
};
constexpr uint8_t kBankCount = sizeof(kBank) / sizeof(kBank[0]);

}  // namespace

void MelodyPlayer::begin(uint8_t gpioPin, uint8_t ledcChannel, uint8_t ampEnablePin) {
    _pin = gpioPin;
    _ch  = ledcChannel;
    // Boards that route the amp's SHUTDOWN input to a GPIO leave it floating, and
    // a floating input can settle above the threshold = amp off. Pull it low once.
    if (ampEnablePin != NO_PIN) {
        pinMode(ampEnablePin, OUTPUT);
        digitalWrite(ampEnablePin, LOW);
    }
    // Configure LEDC once; the pin attaches in play(). Give it a defined idle
    // state now (see stop()). The resolution set here is temporary anyway,
    // ledcWriteTone() in tick() switches the channel to 10 bit.
    ledcSetup(_ch, 2000, 10);
    pinMode(_pin, INPUT_PULLDOWN);
}

uint8_t MelodyPlayer::count() const { return kBankCount; }

void MelodyPlayer::play(uint8_t index, uint8_t masterVolPct, uint32_t maxMs) {
    // Last-wins: stop the current melody first (clean switch), the cutoff then
    // re-anchors to this play.
    if (_active) stop();
    if (index >= kBankCount) index = 0;
    const Melody& m = kBank[index];

    // Effective volume = per-song vol × master (both 0..100 %) -> LEDC duty.
    // The duty is 10-bit: tick() calls ledcWriteTone() first, which reconfigures
    // the channel to 10 bit resolution. So full volume is 512 (= 50 % duty, the
    // loudest a square wave gets), NOT 128 that would only be 12.5 %.
    uint16_t songVol = m.vol > 100 ? 100 : m.vol;
    uint16_t gVol    = masterVolPct > 100 ? 100 : masterVolPct;
    _duty   = (uint16_t)((uint32_t)songVol * gVol * 512 / 10000);

    _idx       = index;
    _noteIdx   = 0;             // start the melody from the top
    _noteEndMs = millis();      // play the first note immediately
    _sounding  = false;
    _maxMs     = maxMs;         // safety cutoff for this playback (0 = none)
    _startMs   = millis();
    _active    = true;
    ledcAttachPin(_pin, _ch);   // route buzzer to LEDC (no heap)
}

void MelodyPlayer::stop() {
    ledcWrite(_ch, 0);          // duty 0 -> silent
    ledcDetachPin(_pin);        // release pin from LEDC (no heap)
    // Hold the pin softly at GND (pulldown, NOT driven LOW) so display-SPI bursts
    // can't couple into the piezo and crackle.
    pinMode(_pin, INPUT_PULLDOWN);
    _sounding = false;
    _active   = false;
}

void MelodyPlayer::tick() {
    // Honour a cross-task stop request on the loop task, so stop()'s LEDC + state
    // mutation never races play()/tick() (see requestStop()).
    if (_stopReq) { _stopReq = false; if (_active) stop(); return; }
    if (!_active) return;

    uint32_t now = millis();
    if (_maxMs > 0 && now - _startMs > _maxMs) {
        stop();
        return;
    }
    if ((int32_t)(now - _noteEndMs) < 0) return;  // current phase not finished

    const Melody& m = kBank[_idx];
    if (_sounding) {
        // Tone phase finished -> short silent gap for articulation.
        ledcWrite(_ch, 0);
        _sounding  = false;
        _noteEndMs = now + NOTE_GAP_MS;
    } else if (_noteIdx >= m.len) {
        _noteIdx   = 0;                       // loop the tune...
        _noteEndMs = now + MELODY_REPEAT_GAP; // ...after a longer pause
    } else {
        const Note& note = m.notes[_noteIdx++];
        uint32_t dur = noteMs(note.div, m.bpm);
        if (note.freq > 0) {
            ledcWriteTone(_ch, note.freq);    // set frequency (50% duty)
            ledcWrite(_ch, _duty);            // override duty -> per-song volume
            _sounding  = true;
            // hold it for the note minus the trailing articulation gap
            _noteEndMs = now + (dur > NOTE_GAP_MS ? dur - NOTE_GAP_MS : dur);
        } else {
            ledcWrite(_ch, 0);                // rest: silent for full dur
            _noteEndMs = now + dur;
        }
    }
}
