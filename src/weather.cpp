#include "weather.h"

#include "globals.h"
#include "icons/icons.h"
#include <ApiClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <Update.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

extern Display   display;
extern PixelFont wartrisFont;

// === Internal types ===

constexpr uint8_t  NUM_SLOTS = 10;   // 2 pages × 5 slots
constexpr uint8_t  PAGE_SIZE = 5;
constexpr uint8_t  ICON_NONE = 0xFF;
constexpr uint32_t RETRY_MS  = 60000;

// open-meteo forecast endpoint (query params appended per fetch below).
#define OPENMETEO_FORECAST_URL "https://api.open-meteo.com/v1/forecast"

// Convert a stored Celsius temperature to the configured display unit.
static inline int displayTemp(int8_t celsius) {
    return dc.fahrenheit ? (int)lroundf((float)celsius * 1.8f + 32.0f)
                         : (int)celsius;                         
}

struct WeatherSlot {
    uint8_t icon;
    int8_t  temp;
    uint8_t hour;
};

// === WeatherWidgetBase ===
// All three weather widgets share the fetched data via static members.

class WeatherWidgetBase : public Widget {
protected:
    static SemaphoreHandle_t _mutex;
    static WeatherSlot       _slots[NUM_SLOTS];
    static bool              _hasData;
    static bool              _dirty;
    static int               _lastFetchSlot;
    static uint32_t          _lastAttemptMs;

    uint8_t _pageOffset;

    static uint8_t iconFromCode(int code, bool isDay);
    static bool    fetchAndFill(WeatherSlot out[]);
    static void    fetch();

    void renderPage(int yPx);

public:
    explicit WeatherWidgetBase(uint8_t pageOffset) : _pageOffset(pageOffset) {}

    void onActivate(int16_t, int16_t) override {
        if (!_mutex) return;
        xSemaphoreTake(_mutex, portMAX_DELAY);
        _dirty = true;
        xSemaphoreGive(_mutex);
    }

    static void init() {
        _mutex = xSemaphoreCreateMutex();
        for (uint8_t i = 0; i < NUM_SLOTS; i++) _slots[i] = { ICON_NONE, 0, 0 };
        _hasData       = false;
        _dirty         = true;
        _lastFetchSlot = -1;
        _lastAttemptMs = 0;
        LOG_SDEBUG("[weather] init");
    }
};

SemaphoreHandle_t WeatherWidgetBase::_mutex        = nullptr;
WeatherSlot       WeatherWidgetBase::_slots[NUM_SLOTS] = {};
bool              WeatherWidgetBase::_hasData       = false;
bool              WeatherWidgetBase::_dirty         = false;
int               WeatherWidgetBase::_lastFetchSlot = -1;
uint32_t          WeatherWidgetBase::_lastAttemptMs = 0;

// === WeatherWidget (page 0, owns the fetch) ===

class WeatherWidget : public WeatherWidgetBase {
public:
    WeatherWidget() : WeatherWidgetBase(0) {}
    const char* name() const override { return "weather"; }
    void update() override { fetch(); }
    void render(uint8_t x, uint8_t y) override {
        (void)x;
        renderPage((int)y * Display::BLOCK_PX);
    }
};

// === WeatherWidget2 (page 1, no fetch inherits no-op update) ===

class WeatherWidget2 : public WeatherWidgetBase {
public:
    WeatherWidget2() : WeatherWidgetBase(PAGE_SIZE) {}
    const char* name() const override { return "weather2"; }
    void render(uint8_t x, uint8_t y) override {
        (void)x;
        renderPage((int)y * Display::BLOCK_PX);
    }
};

// === TempCurrentWidget (current hour temp + icon) ===

class TempCurrentWidget : public WeatherWidgetBase {
    int8_t  _lastVal  = INT8_MIN;
    uint8_t _lastIcon = ICON_NONE;
public:
    TempCurrentWidget() : WeatherWidgetBase(0) {}
    const char* name() const override { return "temp-current"; }

    void onActivate(int16_t, int16_t) override {
        _lastVal  = INT8_MIN;
        _lastIcon = ICON_NONE;
    }

    void render(uint8_t gx, uint8_t gy) override {
        if (!_mutex) return;

        xSemaphoreTake(_mutex, portMAX_DELAY);
        bool    hasData = _hasData;
        int8_t  temp    = hasData ? _slots[0].temp : 0;
        uint8_t icon    = hasData ? _slots[0].icon : ICON_NONE;
        xSemaphoreGive(_mutex);

        if (!hasData) return;
        if (temp == _lastVal && icon == _lastIcon) return;
        _lastVal  = temp;
        _lastIcon = icon;

        char buf[8];
        snprintf(buf, sizeof(buf), "%3d°", displayTemp(temp));
        display.drawText(buf, 1, gx, gy, 11, Display::DrawModeBgFill, dc.big_temp_col);
        display.drawIcon(icon, gx + 4 * 7 - 1, gy + 1, 0, Display::DrawModeBgFill);
    }
};

// === Static method implementations ===

uint8_t WeatherWidgetBase::iconFromCode(int code, bool isDay) {
    switch (code) {
        case 0:  case 1:                 return isDay ? weather_sun       : weather_night;
        case 2:                          return isDay ? weather_sun_cloud : weather_cloud;
        case 3:                          return weather_cloud;
        case 45: case 48:                return weather_fog;
        case 51: case 53: case 55:
        case 56: case 57:
        case 61: case 63: case 65:
        case 66: case 67:
        case 80: case 81: case 82:       return weather_rain;
        case 71: case 73: case 75:
        case 77:
        case 85: case 86:                return weather_snow;
        case 95: case 96: case 99:       return weather_storm;
        default:                         return isDay ? weather_sun_cloud : weather_cloud;
    }
}

bool WeatherWidgetBase::fetchAndFill(WeatherSlot out[]) {
    constexpr const char* TZ  = "auto";

    String url = OPENMETEO_FORECAST_URL "?latitude=";
    url += String(dc.latitude, 4);
    url += "&longitude=";
    url += String(dc.lonlatitude, 4);
    url += "&hourly=weather_code,temperature_2m,is_day&timezone=";
    url += TZ;
    url += "&forecast_days=2";

    ApiClient api;
    api.setTimeout(8000);
    JsonDocument doc;
    int code = api.get(url.c_str(), doc);
    if (code != 200) {
        LOG_SERROR("[weather] fetch failed: code=%d err=%s", code, api.lastError().c_str());
        return false;
    }

    JsonArrayConst times = doc["hourly"]["time"].as<JsonArrayConst>();
    JsonArrayConst codes = doc["hourly"]["weather_code"].as<JsonArrayConst>();
    JsonArrayConst temps = doc["hourly"]["temperature_2m"].as<JsonArrayConst>();
    JsonArrayConst isday = doc["hourly"]["is_day"].as<JsonArrayConst>();

    if (times.isNull() || codes.isNull() || temps.isNull() || isday.isNull()) {
        LOG_SERROR("[weather] missing hourly arrays in JSON");
        return false;
    }

    String nowKey  = tm.getFormattedTimestamp("%Y-%m-%dT%H:00");
    int    startIdx = -1;
    int    idx      = 0;
    for (JsonVariantConst t : times) {
        const char* s = t.as<const char*>();
        if (s && strcmp(s, nowKey.c_str()) == 0) { startIdx = idx; break; }
        idx++;
    }
    if (startIdx < 0) {
        LOG_SERROR("[weather] now (%s) not in forecast times", nowKey.c_str());
        return false;
    }

    const size_t total = times.size();
    for (uint8_t j = 0; j < NUM_SLOTS; j++) {
        size_t k = (size_t)startIdx + j;
        if (k >= total) { out[j].icon = ICON_NONE; continue; }

        int         wcode = codes[k].as<int>();
        float       tempC = temps[k].as<float>();
        bool        isDay = isday[k].as<int>() == 1;
        const char* tStr  = times[k].as<const char*>();

        uint8_t hour = 0;
        if (tStr && strlen(tStr) >= 13)
            hour = (uint8_t)((tStr[11] - '0') * 10 + (tStr[12] - '0'));

        out[j].icon = iconFromCode(wcode, isDay);
        out[j].temp = (int8_t)lroundf(tempC);
        out[j].hour = hour;
    }

    LOG_SDEBUG("[weather] ok startIdx=%d slot0=%d° h=%u icon=%u",
               startIdx, out[0].temp, out[0].hour, out[0].icon);
    return true;
}

void WeatherWidgetBase::fetch() {
    if (!WiFi.isConnected() || !ntp_updated) return;
    if (Update.isRunning()) return;

    int currentHour = tm.getHour();
    int currentMin  = tm.getMinute();
    int currentSlot = (currentHour * 2) + (currentMin >= 30 ? 1 : 0);

    if (_lastFetchSlot == currentSlot) return;
    if (_lastAttemptMs != 0 && (millis() - _lastAttemptMs) < RETRY_MS) return;
    _lastAttemptMs = millis();

    LOG_SDEBUG("[weather] fetch start hour=%d min=%d millis=%lu",
              currentHour, currentMin, (unsigned long)_lastAttemptMs);

    uint32_t t0 = millis();
    WeatherSlot tmp[NUM_SLOTS] = {};
    g_fetchActive = true;
    bool ok = fetchAndFill(tmp);
    g_fetchActive = false;
    LOG_SINFO("[weather] fetch %s in %lu ms", ok ? "OK" : "FAIL", (unsigned long)(millis() - t0));

    xSemaphoreTake(_mutex, portMAX_DELAY);
    if (ok) {
        memcpy(_slots, tmp, sizeof(_slots));
        _hasData       = true;
        _lastFetchSlot = currentSlot;
    } else {
        for (uint8_t i = 0; i < NUM_SLOTS; i++) _slots[i].icon = ICON_NONE;
        _hasData = false;
    }
    _dirty = true;
    xSemaphoreGive(_mutex);
}

void WeatherWidgetBase::renderPage(int yPx) {
    WeatherSlot local[PAGE_SIZE];
    bool hasData;

    xSemaphoreTake(_mutex, portMAX_DELAY);
    if (!_dirty) {
        xSemaphoreGive(_mutex);
        return;
    }
    _dirty  = false;
    hasData = _hasData;
    if (hasData) memcpy(local, _slots + _pageOffset, sizeof(local));
    xSemaphoreGive(_mutex);

    if (!hasData) return;

    uint8_t yGrid = (uint8_t)(yPx >> 3);
    char tbuf[10];
    char hbuf[10];

    for (uint8_t i = 0; i < PAGE_SIZE; i++) {
        if (local[i].icon == ICON_NONE) continue;
        display.drawIcon(local[i].icon, i * 8, yGrid, 0, Display::DrawModeBgFill);
        snprintf(tbuf, sizeof(tbuf), "%3d°", displayTemp(local[i].temp));
        if (dc.clock_24h) {
            snprintf(hbuf, sizeof(hbuf), "%02u:00", local[i].hour);
        } else {
            uint8_t h12 = local[i].hour % 12;
            if (h12 == 0) h12 = 12;
            snprintf(hbuf, sizeof(hbuf), "%02u%s", h12, local[i].hour < 12 ? "AM" : "PM");
        }
        uint8_t tcol = dc.small_temp_col > 0 ? (uint8_t)dc.small_temp_col : Display::COLOR_NONE;
        display.drawPixelText(wartrisFont, tbuf, i * 64 + 8, yPx + 64,     Display::DrawModeBgFill, tcol);
        display.drawPixelText(wartrisFont, hbuf, i * 64 + 8, yPx + 64 + 8, Display::DrawModeBgFill);
    }
}

// === Public instances ===

static WeatherWidget     _widget_weather;
static WeatherWidget2    _widget_weather2;
static TempCurrentWidget _widget_temp_current;

Widget& widget_weather      = _widget_weather;
Widget& widget_weather2     = _widget_weather2;
Widget& widget_temp_current = _widget_temp_current;

void weatherInit() {
    WeatherWidgetBase::init();
}
