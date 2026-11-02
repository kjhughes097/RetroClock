#pragma once

#include <stdint.h>
#include "serlog.h"

#include "ColorMatrix.h"
//#include "DrawMode.h"
#include "Font.h"
#include "Icon.h"
#include "PixelFont.h"

class Display {
public:
    enum {
        DrawModeMask   = 0,
        DrawModeBgFill = 1,
        DrawModeSkip   = 2,
    };
    static constexpr uint8_t BLOCK_PX   = 8;
    static constexpr uint8_t GRID_W     = 40;
    static constexpr uint8_t GRID_H     = 30;
    static constexpr uint8_t MAX_FONTS  = 8;
    static constexpr uint8_t COLOR_NONE = 0xFF;  // sentinel for "no color override"

    Display();
    ~Display();

    Display(const Display&)            = delete;
    Display& operator=(const Display&) = delete;

    // Initializes the panel in a landscape default orientation. Set the final
    // orientation afterwards via setRotation() once the config is known.
    // The panel controller (ST7789/ILI9341) is autodetected via RDDID (0x04);
    // 'togglePanel' selects the other one for boards the detection gets wrong.
    bool begin(const char* colorMapPath = "/ColorMap.bmp", bool togglePanel = false);

    // Panel driver actually in use (autodetect after the optional toggle); only
    // valid after begin(). Doubles as the board revision marker, the two CYD
    // variants differ beyond the panel itself (e.g. the audio amp wiring).
    const char* panelName() const { return _panelIsST7789 ? "ST7789" : "ILI9341"; }
    bool panelIsST7789() const { return _panelIsST7789; }

    // rotation: 1 = landscape, 3 = landscape flipped (CYD: USB on the right).
    // The grid/background buffers are fixed landscape, so only 1 and 3 are valid.
    // Values are relative to the ST7789 boards; ILI9341 ones are flipped by 180 deg
    // internally so the same setting looks the same on both.
    void setRotation(uint8_t rotation);

    void fillBackground(uint8_t color);
    void drawRectangle(uint8_t color, uint8_t x, uint8_t y, uint8_t w, uint8_t h);

    void registerFont(uint8_t fontIndex, const Font& f);
    void registerIconSet(const IconSet& s);

    // 'lines' limits rendered rows per glyph/icon from the top (0 = all; values
    // above the glyph height just draw the existing rows, no padding).
    void drawText(const char* text, uint8_t fontIndex, uint8_t x, uint8_t y,
                  uint8_t lines, uint8_t mode, uint8_t colorOverride = COLOR_NONE);
    void drawIcon(uint8_t iconIndex, uint8_t x, uint8_t y,
                  uint8_t lines, uint8_t mode, uint8_t colorOverride = COLOR_NONE);

    // Pixel-level bitmap font (multi-color RGB565). px/py are in DISPLAY PIXELS,
    // not grid cells; each char advances the cursor by f.charW() pixels.
    void drawPixelText(const PixelFont& f, const char* text,
                       int px, int py, uint8_t mode,
                       uint8_t colorOverride = COLOR_NONE);

    // Background color of a specific cell (palette index 0..31). Out-of-bounds returns 0.
    uint8_t bgAt(uint8_t gx, uint8_t gy) const {
        return (gx < GRID_W && gy < GRID_H) ? _bg[(uint16_t)gy * GRID_W + gx] : 0;
    }
    // Convenience: background of the cell containing the given display-pixel coords.
    uint8_t bgAtPx(int px, int py) const {
        if (px < 0 || py < 0) return 0;
        return bgAt((uint8_t)(px / BLOCK_PX), (uint8_t)(py / BLOCK_PX));
    }
    // Returns true when display is touched; x/y in display pixels (rotation-aware). Both are optional.
    bool getTouch(int16_t* x = nullptr, int16_t* y = nullptr);

    // Backlight (CYD: PWM on GPIO 21); level 0 = off, 255 = full. displayOff()
    // blanks it (content + touch stay live); displayOn() restores the last level.
    void setBrightness(uint8_t level);
    void displayOff();
    void displayOn();
    bool isDisplayOn() const { return _backlight > 0; }

    // Panel-level color inversion (INVON/INVOFF): no pixel redraw needed, so it
    // is cheap enough to toggle for a full-screen flash. Relative to the panel's
    // configured invert setting, i.e. false = normal colors.
    void setInvert(bool on);

    bool isReady() const;

private:
    uint8_t normalizeRotation(uint8_t rotation) const;
    void fillPixelRect(int px, int py, int pw, int ph, uint16_t rgb565);
    void drawBlock(uint8_t colorIdx, uint8_t gx, uint8_t gy);
    void drawBitmap(const uint8_t* data, uint8_t w, uint8_t h, uint8_t gx, uint8_t gy,
                    uint8_t lines, uint8_t mode, uint8_t colorOverride);

    void*              _tft;  // opaque pointer to internal LGFX_CYD
    uint8_t            _backlight   = 255;  // current backlight level (0 = off)
    uint8_t            _lastOnLevel = 255;  // brightness to restore on displayOn()
    bool               _panelIsST7789 = true;  // panel driver picked in begin()
    ColorMatrix        _matrix;
    // Per-cell background color (palette index 0..31). Updated by fillBackground and
    // drawRectangle; read by BgFill mode in drawText / drawIcon / drawPixelText.
    uint8_t            _bg[GRID_W * GRID_H];
    const Font*        _fonts[MAX_FONTS];
    const IconSet*     _iconSet;

    // Debounce for getTouch(): the XPT2046 glitches at press/release edges, so a
    // raw reading must hold steady for TOUCH_DEBOUNCE_MS before it's accepted.
    bool     _touchRawPrev  = false;
    bool     _touchStable   = false;
    uint32_t _touchChangeMs = 0;
    int16_t  _lastTx        = 0;
    int16_t  _lastTy        = 0;
};
