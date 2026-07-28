#pragma once

#include <stdint.h>

// Grid-cell rectangle. w==0 means "disabled" (hit-test always false).
struct Rect {
    uint8_t x, y, w, h;
};

// True if display-pixel (px,py) lies inside grid-cell Rect r. w==0 -> always false.
bool rectContainsPx(const Rect& r, int16_t px, int16_t py);

class Widget {
public:
    virtual const char* name() const = 0;
    virtual void render(uint8_t x, uint8_t y) = 0;
    virtual void update() {}

    // Called when this widget becomes the active candidate in its slot
    // either from a real touch inside the slot (txPx/tyPx = the touch's
    // display-pixel coords) or from a screen-entry sweep (txPx/tyPx = -1,-1,
    // meaning "no real touch just (re)paint"). Widgets that don't care
    // about sub-slot touch position can ignore the parameters.
    virtual void onActivate(int16_t txPx = -1, int16_t tyPx = -1) { (void)txPx; (void)tyPx; }
    virtual ~Widget() = default;
};

// A position on screen that holds 1-4 swappable widget candidates.
struct Slot {
    Rect    area;            // grid-cell bounds; render origin and touch hit-test
    Widget* candidates[4];
    uint8_t numCandidates;
    uint8_t active;          // index into candidates[]
};

// A full-screen view containing slots and optional prev/next touch zones.
struct Screen {
    const char* name;
    Slot*   slots;
    uint8_t numSlots;
    Rect    prevZone;   // w==0 = disabled
    Rect    nextZone;
    uint8_t bgColor;
};

// Call once in setup(). Starts the background update task.
// initialIdx selects which screen is active on boot (clamped to [0, count-1]).
void screensInit(Screen** screens, uint8_t count, uint8_t initialIdx = 0);

// Call every loop iteration renders all slots on the active screen.
void screensRender();

// Call on edge-detected press dispatches to prev/next zone or slot cycle.
void screensHandleTouch(int16_t txPx, int16_t tyPx);

// Activate screen by index, fill background, invalidate widgets.
void screensSwitchTo(uint8_t idx);

// Index of the currently active screen (as passed to screensInit/SwitchTo).
uint8_t screensActiveIndex();
