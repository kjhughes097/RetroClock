#include "screens.h"

#include "globals.h"
#include <RetroGfx.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_heap_caps.h>

extern Display display;

bool rectContainsPx(const Rect& r, int16_t px, int16_t py) {
    if (r.w == 0) return false;
    int16_t x0 = (int16_t)r.x * Display::BLOCK_PX;
    int16_t y0 = (int16_t)r.y * Display::BLOCK_PX;
    int16_t x1 = x0 + (int16_t)r.w * Display::BLOCK_PX;
    int16_t y1 = y0 + (int16_t)r.h * Display::BLOCK_PX;
    return px >= x0 && px < x1 && py >= y0 && py < y1;
}

namespace {

Screen**     _screens    = nullptr;
uint8_t      _numScreens = 0;
uint8_t      _active     = 0;
TaskHandle_t _bgTask     = nullptr;

void bgUpdateTaskBody(void*) {
    // Wait for WiFi + NTP before first update tick.
    while (!WiFi.isConnected()) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    for (;;) {
        LOG_SDEBUG("[screens] bg update tick (wifi=%d ntp=%d) heap: free=%u max_block=%u",
                  (int)WiFi.isConnected(), (int)ntp_updated,
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        if (WiFi.isConnected()) {
            for (uint8_t s = 0; s < _numScreens; s++) {
                Screen* scr = _screens[s];
                for (uint8_t k = 0; k < scr->numSlots; k++) {
                    Slot& slot = scr->slots[k];
                    for (uint8_t c = 0; c < slot.numCandidates; c++) {
                        Widget* w = slot.candidates[c];
                        if (w) w->update();
                    }
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}

}  // namespace

void screensInit(Screen** screens, uint8_t count, uint8_t initialIdx) {
    _screens    = screens;
    _numScreens = count;
    _active     = (initialIdx < count) ? initialIdx : 0;

    display.fillBackground(_screens[_active]->bgColor);

    Screen* scr = _screens[_active];
    for (uint8_t k = 0; k < scr->numSlots; k++) {
        Slot& slot = scr->slots[k];
        Widget* w = slot.candidates[slot.active];
        if (w) w->onActivate();
    }

    xTaskCreatePinnedToCore(bgUpdateTaskBody, "bgUpdate", 8192, nullptr, 1, &_bgTask, 0);
    LOG_SDEBUG("[screens] active=%d name=%s", (int)_active, _screens[_active]->name);
    LOG_SDEBUG("[screens] bg task started on core 0");
}

void screensRender() {
    if (!_screens || _numScreens == 0) return;
    Screen* scr = _screens[_active];
    for (uint8_t k = 0; k < scr->numSlots; k++) {
        Slot& slot = scr->slots[k];
        Widget* w = slot.candidates[slot.active];
        if (w) w->render(slot.area.x, slot.area.y);
    }
}

uint8_t screensActiveIndex() {
    return _active;
}

void screensSwitchTo(uint8_t idx) {
    if (!_screens || idx >= _numScreens) return;
    LOG_SINFO("[screens] switch %d->%d name=%s",
              (int)_active, (int)idx, _screens[idx]->name);
    _active = idx;
    Screen* scr = _screens[idx];
    display.fillBackground(scr->bgColor);
    for (uint8_t k = 0; k < scr->numSlots; k++) {
        Slot& slot = scr->slots[k];
        Widget* w = slot.candidates[slot.active];
        if (w) w->onActivate();
    }
}

void screensHandleTouch(int16_t txPx, int16_t tyPx) {
    if (!_screens || _numScreens == 0) return;
    Screen* scr = _screens[_active];

    static uint32_t lastSwitchMs = 0;
    bool inPrev = rectContainsPx(scr->prevZone, txPx, tyPx);
    bool inNext = rectContainsPx(scr->nextZone, txPx, tyPx);
    if (inPrev || inNext) {
        uint32_t now = millis();
        if (now - lastSwitchMs < 500) return;
        lastSwitchMs = now;
        if (inPrev) screensSwitchTo((_active + _numScreens - 1) % _numScreens);
        else        screensSwitchTo((_active + 1) % _numScreens);
        return;
    }
    for (uint8_t k = 0; k < scr->numSlots; k++) {
        Slot& slot = scr->slots[k];
        if (rectContainsPx(slot.area, txPx, tyPx)) {
            if (slot.numCandidates == 0) return;   // guard modulo-by-zero (ESP32 panic)
            uint8_t next = (slot.active + 1) % slot.numCandidates;
            LOG_SDEBUG("[screens] slot %d: %d->%d", (int)k, (int)slot.active, (int)next);
            slot.active = next;
            Widget* w = slot.candidates[next];
            if (w) w->onActivate(txPx, tyPx);
            return;
        }
    }
}
