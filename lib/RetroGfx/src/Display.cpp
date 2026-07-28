#include "Display.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <type_traits>

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

namespace {

// CYD TFT bus pins (HSPI). Kept here as well because the panel autodetect below
// talks to the controller before any LovyanGFX object exists.
constexpr int PIN_TFT_SCLK = 14;
constexpr int PIN_TFT_MOSI = 13;
constexpr int PIN_TFT_MISO = 12;
constexpr int PIN_TFT_DC   = 2;
constexpr int PIN_TFT_CS   = 15;

// The CYD 2432S028 ships with two panel controllers depending on the batch:
// ST7789 or ILI9341. Same PCB/pins, only the controller class differs a wrong
// class gives a white/garbled screen. Both variants are compiled in and picked
// at runtime, so the template only varies the panel type.
template <typename PanelT>
class LGFX_CYD : public lgfx::LGFX_Device {
    PanelT               _panel;
    lgfx::Bus_SPI        _bus;
    lgfx::Light_PWM      _light;
    lgfx::Touch_XPT2046  _touch;

public:
    LGFX_CYD() {
        {
            auto cfg        = _bus.config();
            cfg.spi_host    = HSPI_HOST;  // CYD TFT is on HSPI/SPI2
            cfg.spi_mode    = 0;
            cfg.freq_write  = 55000000;
            cfg.freq_read   = 6000000;   // ILI9341/ST7789 spec reads at <= 6.6 MHz
            cfg.spi_3wire   = false;
            cfg.use_lock    = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk    = PIN_TFT_SCLK;
            cfg.pin_mosi    = PIN_TFT_MOSI;
            cfg.pin_miso    = PIN_TFT_MISO;
            cfg.pin_dc      = PIN_TFT_DC;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {
            auto cfg             = _panel.config();
            cfg.pin_cs           = PIN_TFT_CS;
            cfg.pin_rst          = -1;
            cfg.pin_busy         = -1;
            cfg.panel_width      = 240;
            cfg.panel_height     = 320;
            cfg.offset_x         = 0;
            cfg.offset_y         = 0;
            cfg.offset_rotation  = 0;
            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits  = 1;
            cfg.readable         = true;
            cfg.invert           = false;
            cfg.rgb_order        = false;
            cfg.dlen_16bit       = false;
            cfg.bus_shared       = false;
            _panel.config(cfg);
        }
        {
            auto cfg        = _light.config();
            cfg.pin_bl      = 21;
            cfg.invert      = false;
            cfg.freq        = 44100;
            cfg.pwm_channel = 7;
            _light.config(cfg);
            _panel.setLight(&_light);
        }
        {
            auto cfg            = _touch.config();
            cfg.x_min           = 0;
            cfg.x_max           = 4095;
            cfg.y_min           = 0;
            cfg.y_max           = 4095;
            cfg.pin_int         = 36;
            cfg.bus_shared      = false;
            // Counters the 180 deg rotation flip that Display::normalizeRotation()
            // applies on ILI9341 boards, so the touch orientation ends up the same
            // on both: LovyanGFX adds panel rotation and touch offset before use.
            cfg.offset_rotation = std::is_same<PanelT, lgfx::Panel_ILI9341>::value ? 6 : 4;
            cfg.spi_host        = VSPI_HOST;
            cfg.freq            = 1000000;
            cfg.pin_sclk        = 25;
            cfg.pin_mosi        = 32;
            cfg.pin_miso        = 39;
            cfg.pin_cs          = 33;
            _touch.config(cfg);
            _panel.setTouch(&_touch);
        }
        setPanel(&_panel);
    }
};

// Sends a command without parameters, framed by CS. A leading NOP with CS high
// syncs the controller's command state machine (LovyanGFX does the same in its
// own board autodetect).
void writePanelCmd(lgfx::Bus_SPI& bus, uint_fast16_t cmd) {
    bus.beginTransaction();
    lgfx::gpio_hi(PIN_TFT_CS);
    bus.writeCommand(0, 8);       // NOP with CS high
    bus.wait();
    lgfx::gpio_lo(PIN_TFT_CS);
    bus.writeCommand(cmd, 8);
    bus.wait();
    bus.endTransaction();
    lgfx::gpio_hi(PIN_TFT_CS);
}

// Reads a panel register the same way: command, then a single dummy read bit.
// The 4 reply bytes come back with the first one in the low byte.
uint32_t readPanelReg(lgfx::Bus_SPI& bus, uint_fast16_t cmd) {
    bus.beginTransaction();
    lgfx::gpio_hi(PIN_TFT_CS);
    bus.writeCommand(0, 8);
    bus.wait();
    lgfx::gpio_lo(PIN_TFT_CS);
    bus.writeCommand(cmd, 8);
    bus.beginRead(1);             // 1 dummy bit before the reply
    uint32_t res = 0;
    for (size_t i = 0; i < 4; i++) res += (bus.readData(8) & 0xFF) << (i * 8);
    bus.endTransaction();
    lgfx::gpio_hi(PIN_TFT_CS);
    return res;
}

// True = ST7789, false = ILI9341. Runs BEFORE the panel object exists, over a
// temporary SPI bus (8 MHz, no DMA, released again afterwards).
// RDDID (0x04) returns the manufacturer in its first byte: 0x85 = ST7789,
// 0x00 = ILI9341. A 0 can also mean "nothing answered" though, so it only counts
// as ILI9341 when the display status (0x09) replies as well; if the panel stays
// silent the ST7789 default applies (newer CYD batches).
bool detectPanelIsST7789() {
    lgfx::Bus_SPI bus;
    {
        auto cfg        = bus.config();
        cfg.spi_host    = HSPI_HOST;
        cfg.spi_mode    = 0;
        cfg.freq_write  = 8000000;
        cfg.freq_read   = 8000000;
        cfg.spi_3wire   = false;
        cfg.use_lock    = true;
        cfg.dma_channel = 0;    // no DMA while detecting, the channel would stick
        cfg.pin_sclk    = PIN_TFT_SCLK;
        cfg.pin_mosi    = PIN_TFT_MOSI;
        cfg.pin_miso    = PIN_TFT_MISO;
        cfg.pin_dc      = PIN_TFT_DC;
        bus.config(cfg);
    }
    bus.init();
    lgfx::pinMode(PIN_TFT_CS, lgfx::pin_mode_t::output);

    // The CYD has no RST line, so a soft reboot leaves the controller in whatever
    // state the previous run put it in and its ID answer becomes unreliable (a
    // boot with the wrong driver would then survive every reboot until the board
    // is unplugged). SWRESET restores the power-on state first.
    writePanelCmd(bus, 0x01);
    delay(150);                   // datasheet: >= 120 ms after a software reset

    uint32_t rddid = readPanelReg(bus, 0x04);
    uint32_t id1   = rddid & 0xFF;
    uint32_t rddst = (id1 == 0x00) ? readPanelReg(bus, 0x09) & 0xFFFFFF : 0;
    bus.release();                // free the bus for the panel's own Bus_SPI

    bool st = (id1 != 0x00) || (rddst == 0);
    LOG_SINFO("[Display] RDDID 0x%08X RDDST 0x%06X -> %s",
              (unsigned)rddid, (unsigned)rddst, st ? "ST7789" : "ILI9341");
    return st;
}

}  // namespace

Display::Display() : _tft(nullptr), _iconSet(nullptr) {
    for (uint8_t i = 0; i < MAX_FONTS; i++) _fonts[i] = nullptr;
    for (uint16_t i = 0; i < (uint16_t)GRID_W * GRID_H; i++) _bg[i] = 0;
}

Display::~Display() {
    delete static_cast<lgfx::LGFX_Device*>(_tft);
}

bool Display::isReady() const {
    return _tft != nullptr && _matrix.isLoaded();
}

void Display::setBrightness(uint8_t level) {
    if (!_tft) return;
    static_cast<lgfx::LGFX_Device*>(_tft)->setBrightness(level);
    _backlight = level;
    if (level > 0) _lastOnLevel = level;   // remember for displayOn()
}

void Display::displayOff() { setBrightness(0); }
void Display::displayOn()  { setBrightness(_lastOnLevel > 0 ? _lastOnLevel : 255); }

// The ILI9341 CYDs have their glass mounted 180 deg to the ST7789 ones, so the
// same rotation setting has to be flipped for them (1 <-> 3, 0 <-> 2). Callers
// keep passing the configured value, the touch offset above compensates.
uint8_t Display::normalizeRotation(uint8_t rotation) const {
    return _panelIsST7789 ? (rotation & 0x03) : ((rotation & 0x03) ^ 2);
}

void Display::setRotation(uint8_t rotation) {
    if (!_tft) return;
    static_cast<lgfx::LGFX_Device*>(_tft)->setRotation(normalizeRotation(rotation));
}

bool Display::begin(const char* colorMapPath, bool togglePanel) {
    if (!_tft) {
        // Autodetect the panel controller before it gets initialized; 'togglePanel'
        // inverts the result for boards the detection guesses wrong.
        _panelIsST7789 = detectPanelIsST7789();
        if (togglePanel) _panelIsST7789 = !_panelIsST7789;
        LOG_SINFO("[Display] panel driver %s%s", panelName(),
                  togglePanel ? " (toggled)" : "");

        _tft = _panelIsST7789
                   ? static_cast<lgfx::LGFX_Device*>(new LGFX_CYD<lgfx::Panel_ST7789>())
                   : static_cast<lgfx::LGFX_Device*>(new LGFX_CYD<lgfx::Panel_ILI9341>());
    }
    auto* tft = static_cast<lgfx::LGFX_Device*>(_tft);
    tft->init();
    tft->setRotation(normalizeRotation(3));   // landscape default; caller overrides via setRotation()
    tft->setBrightness(200);
    tft->setSwapBytes(true);  // tile pixels are stored as standard RGB565 (host-endian)
    tft->fillScreen(0x0000);
    LOG_SINFO("[Display] panel %dx%d (rotation %u)",
                  tft->width(), tft->height(), (unsigned)tft->getRotation());


    if (!LittleFS.begin(false)) {
        LOG_SERROR("[Display] LittleFS mount failed, retrying with format-on-fail");
        if (!LittleFS.begin(true)) {
            LOG_SERROR("[Display] LittleFS mount failed permanently");
            return false;
        }
    }

    if (!_matrix.loadFromFile(colorMapPath)) {
        LOG_SERROR("[Display] ColorMatrix load failed");
        return false;
    }

    return true;
}

void Display::fillPixelRect(int px, int py, int pw, int ph, uint16_t rgb565) {
    auto* tft = static_cast<lgfx::LGFX_Device*>(_tft);
    tft->fillRect(px, py, pw, ph, rgb565);
}

void Display::drawBlock(uint8_t colorIdx, uint8_t gx, uint8_t gy) {
    if (gx >= GRID_W || gy >= GRID_H) return;
    auto* tft = static_cast<lgfx::LGFX_Device*>(_tft);
    tft->pushImage(gx * BLOCK_PX, gy * BLOCK_PX, BLOCK_PX, BLOCK_PX,
                   _matrix.tilePixels(colorIdx));
}

void Display::fillBackground(uint8_t color) {
    drawRectangle(color, 0, 0, GRID_W, GRID_H);
}

void Display::drawRectangle(uint8_t color, uint8_t x, uint8_t y, uint8_t w, uint8_t h) {
    if (!isReady()) return;
    if (x >= GRID_W || y >= GRID_H) return;
    if (x + w > GRID_W) w = GRID_W - x;
    if (y + h > GRID_H) h = GRID_H - y;
    uint8_t idx = color & 0x1F;
    for (uint8_t row = 0; row < h; row++) {
        for (uint8_t col = 0; col < w; col++) {
            // Track per-cell background AND paint the tile.
            _bg[(uint16_t)(y + row) * GRID_W + (x + col)] = idx;
            drawBlock(idx, x + col, y + row);
        }
    }
}

void Display::registerFont(uint8_t fontIndex, const Font& f) {
    if (fontIndex >= MAX_FONTS) return;
    _fonts[fontIndex] = &f;
}

void Display::registerIconSet(const IconSet& s) {
    _iconSet = &s;
}

void Display::drawBitmap(const uint8_t* data, uint8_t w, uint8_t h,
                         uint8_t gx, uint8_t gy, uint8_t lines,
                         uint8_t mode, uint8_t colorOverride) {
    const bool    override_active = (colorOverride != COLOR_NONE);
    const uint8_t override_color  = colorOverride & 0x1F;
    const uint8_t rowsToDraw      = (lines == 0 || lines > h) ? h : lines;

    for (uint8_t row = 0; row < rowsToDraw; row++) {
        for (uint8_t col = 0; col < w; col++) {
            uint8_t src = data[row * w + col];
            uint8_t color;
            switch (mode) {
                case DrawModeMask:
                    if (src == ColorMatrix::TRANSPARENT) {
                        color = 0;  // 32 & 31 == 0 (preserve original mask semantics)
                    } else {
                        color = override_active ? override_color : (src & 0x1F);
                    }
                    drawBlock(color, gx + col, gy + row);
                    break;
                case DrawModeBgFill:
                    if (src == ColorMatrix::TRANSPARENT) {
                        color = bgAt(gx + col, gy + row);  // per-cell local bg
                    } else {
                        color = override_active ? override_color : (src & 0x1F);
                    }
                    drawBlock(color, gx + col, gy + row);
                    break;
                case DrawModeSkip:
                    if (src != ColorMatrix::TRANSPARENT) {
                        color = override_active ? override_color : (src & 0x1F);
                        drawBlock(color, gx + col, gy + row);
                    }
                    break;
            }
        }
    }
}

// Decode one UTF-8 codepoint from *ptr, advancing past it. Returns codepoint.
// On malformed input falls back to single-byte advance.
static uint32_t utf8_next(const char** ptr) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(*ptr);
    uint32_t cp;
    int len;
    // Continuation bytes are validated with && short-circuit BEFORE the next byte
    // is read, so a truncated sequence (a '\0' where a continuation is expected)
    // falls through to the single-byte branch instead of reading past the string.
    if (p[0] < 0x80) {
        cp = p[0];
        len = 1;
    } else if ((p[0] & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
        cp = ((uint32_t)(p[0] & 0x1F) << 6) | (p[1] & 0x3F);
        len = 2;
    } else if ((p[0] & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
        cp = ((uint32_t)(p[0] & 0x0F) << 12) | ((uint32_t)(p[1] & 0x3F) << 6) | (p[2] & 0x3F);
        len = 3;
    } else if ((p[0] & 0xF8) == 0xF0 && (p[1] & 0xC0) == 0x80 &&
               (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) {
        cp = ((uint32_t)(p[0] & 0x07) << 18) | ((uint32_t)(p[1] & 0x3F) << 12) |
             ((uint32_t)(p[2] & 0x3F) << 6) | (p[3] & 0x3F);
        len = 4;
    } else {
        cp = p[0];   // malformed lead or truncated sequence -> single-byte advance
        len = 1;
    }
    *ptr += len;
    return cp;
}

void Display::drawText(const char* text, uint8_t fontIndex,
                       uint8_t x, uint8_t y, uint8_t lines,
                       uint8_t mode, uint8_t colorOverride) {
    if (!isReady() || !text) return;
    if (fontIndex >= MAX_FONTS || !_fonts[fontIndex]) return;
    const Font& f = *_fonts[fontIndex];

    uint8_t cursorX = x;
    const char* p = text;
    while (*p) {
        uint32_t input_cp = utf8_next(&p);

        // Walk f.charset (also UTF-8) to find the codepoint's index.
        int matchIdx = -1;
        const char* q = f.charset;
        for (uint8_t i = 0; i < f.numChars && *q; i++) {
            uint32_t cs_cp = utf8_next(&q);
            if (cs_cp == input_cp) {
                matchIdx = i;
                break;
            }
        }
        if (matchIdx >= 0) {
            const uint8_t* glyph = f.glyphs + (uint32_t)matchIdx * f.charW * f.charH;
            drawBitmap(glyph, f.charW, f.charH, cursorX, y, lines, mode, colorOverride);
        }
        cursorX += f.charW;
        if (cursorX >= GRID_W) break;
    }
}

void Display::drawIcon(uint8_t iconIndex, uint8_t x, uint8_t y,
                       uint8_t lines, uint8_t mode, uint8_t colorOverride) {
    if (!isReady() || !_iconSet) return;
    if (iconIndex >= _iconSet->count) return;
    const Icon& ic = _iconSet->icons[iconIndex];
    drawBitmap(ic.data, ic.width, ic.height, x, y, lines, mode, colorOverride);
}

void Display::drawPixelText(const PixelFont& f, const char* text,
                            int px, int py, uint8_t mode,
                            uint8_t colorOverride) {
    if (!isReady() || !text || !f.isLoaded()) return;
    auto* tft = static_cast<lgfx::LGFX_Device*>(_tft);

    const uint16_t TRANSP            = PixelFont::TRANSPARENT_RGB565;
    const bool     override_active   = (colorOverride != COLOR_NONE);
    const uint16_t override_rgb565   = override_active ? _matrix.soloColor(colorOverride & 0x1F) : 0;

    const uint8_t cw = f.charW();
    const uint8_t ch = f.charH();
    const int     n  = (int)cw * (int)ch;

    // Fast path: Skip + no override -> push glyph buffer directly with TRANSP color-key,
    // no per-pixel processing required and no temp buffer needed.
    const bool fast_path = (mode == DrawModeSkip) && !override_active;

    // Slow path uses a stack scratch buffer; large enough for up to 32x32 chars.
    constexpr int SCRATCH_SIZE = 32 * 32;
    uint16_t scratch[SCRATCH_SIZE];
    if (!fast_path && n > SCRATCH_SIZE) {
        LOG_SERROR("[Display] drawPixelText: char %dx%d too big for scratch (max 32x32)\n", cw, ch);
        return;
    }

    int cursorX = px;
    const char* p = text;
    while (*p) {
        uint32_t input_cp = utf8_next(&p);

        int matchIdx = -1;
        const char* q = f.charset();
        for (uint16_t i = 0; i < f.numChars() && *q; i++) {
            uint32_t cs_cp = utf8_next(&q);
            if (cs_cp == input_cp) {
                matchIdx = i;
                break;
            }
        }

        if (matchIdx >= 0) {
            const uint16_t* glyph = f.glyphPixels((uint16_t)matchIdx);

            if (fast_path) {
                tft->pushImage(cursorX, py, cw, ch, glyph, TRANSP);
            } else {
                for (int yy = 0; yy < ch; yy++) {
                    for (int xx = 0; xx < cw; xx++) {
                        int      i        = yy * cw + xx;
                        uint16_t src      = glyph[i];
                        bool     is_transp = (src == TRANSP);
                        switch (mode) {
                            case DrawModeMask:
                            case DrawModeBgFill:
                                if (is_transp) {
                                    scratch[i] = _matrix.soloColor(bgAtPx(cursorX + xx, py + yy));
                                } else {
                                    scratch[i] = override_active ? override_rgb565 : src;
                                }
                                break;
                            case DrawModeSkip:
                                scratch[i] = is_transp ? TRANSP : override_rgb565;
                                break;
                        }
                    }
                }
                if (mode == DrawModeSkip) {
                    tft->pushImage(cursorX, py, cw, ch, scratch, TRANSP);
                } else {
                    tft->pushImage(cursorX, py, cw, ch, scratch);
                }
            }
        }
        cursorX += cw;
    }
}

bool Display::getTouch(int16_t* x, int16_t* y) {
    if (!_tft) return false;
    constexpr uint32_t TOUCH_DEBOUNCE_MS = 30;

    auto* tft = static_cast<lgfx::LGFX_Device*>(_tft);
    uint16_t tx = 0, ty = 0;
    bool rawHit = tft->getTouch(&tx, &ty);
    if (rawHit) {
        _lastTx = (int16_t)tx;
        _lastTy = (int16_t)ty;
    }

    uint32_t now = millis();
    if (rawHit != _touchRawPrev) {
        _touchRawPrev  = rawHit;
        _touchChangeMs = now;
    }
    if (now - _touchChangeMs >= TOUCH_DEBOUNCE_MS) {
        _touchStable = rawHit;
    }

    if (_touchStable) {
        if (x) *x = _lastTx;
        if (y) *y = _lastTy;
    }
    return _touchStable;
}
