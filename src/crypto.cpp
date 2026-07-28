#include "crypto.h"

#include "globals.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <Update.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern Display   display;
extern PixelFont wartrisFont1x2;
extern PixelFont wartrisFont2x3;

// === Binance USDT-M Futures REST API (host defined once, reused below) ===
#define BINANCE_FAPI        "https://fapi.binance.com"
#define BINANCE_URL_TICKER  BINANCE_FAPI "/fapi/v1/ticker/price?symbol=%s"
#define BINANCE_URL_KLINES  BINANCE_FAPI "/fapi/v1/indexPriceKlines?pair=%s&interval=%s&limit=%u"

// === Tables ===

constexpr uint8_t  NUM_ASSETS    = 6;
constexpr uint8_t  NUM_INTERVALS = 3;    // [0]=1h  [1]=1d  [2]=1w
// 100 bars/interval: 6×3×100×4 B = 7.2 KB heap, leaves room for the TLS handshake.
// Covers all 9 timeframe offsets (max 52 = 1Y); deeper history would need PSRAM.
constexpr uint16_t HISTORY_DEPTH = 100;

struct AssetDef {
    char    name[8];    // display label, max 7 chars
    char    symbol[16]; // Binance Futures pair, max 15 chars
    uint8_t digits;     // decimal places (truncated if total > 10 chars)
};
// Populated at runtime from dc.crypto_assets in cryptoInit().
// NUM_ASSETS is the hard maximum; s_numAssets holds the actual count (1–6).
static AssetDef s_assets[NUM_ASSETS];
static uint8_t  s_numAssets = 0;

struct TimeframeDef {
    const char* label;
    uint8_t     bucket;   // 0=1h  1=1d  2=1w
    uint16_t    offset;   // g_history index of reference bar (bars back from newest)
};
// 9 timeframes, all offsets ≤ HISTORY_DEPTH. 6M uses the weekly bucket
// (26 bars ≈ 6 months); deeper (3Y = 156 weekly bars) would exceed 100.
static const TimeframeDef s_timeframes[] = {
    {"1H",  0,  1},
    {"4H",  0,  4},
    {"12H", 0, 12},
    {"1D",  1,  1},
    {"1W",  2,  1},
    {"1M",  1, 30},
    {"3M",  2, 13},
    {"6M",  2, 26},   // weekly bucket: 26 bars ≈ 6 months
    {"1Y",  2, 52},
};
constexpr uint8_t NUM_TIMEFRAMES = (uint8_t)(sizeof(s_timeframes) / sizeof(s_timeframes[0]));

static const char* const s_intervalStr[NUM_INTERVALS] = {"1h", "1d", "1w"};

// === Shared State ===
// Newest-first: g_history[a][i][0] = most recent bar. Heap-allocated in
// cryptoInit() (avoids ~4.3 KB BSS overflow); indexed like a static 3-D array.
static float (*g_history)[NUM_INTERVALS][HISTORY_DEPTH] = nullptr;  // 4 B BSS
static float   g_currentPrice[NUM_ASSETS];
static bool    g_stale[NUM_ASSETS];

// Per-asset/interval fetch status: false = last attempt failed (needs retry).
// Set by fetchHistoryStreaming(); checked by the per-minute retry logic.
static bool    g_fetchOk[NUM_ASSETS][NUM_INTERVALS];
static bool    s_historyLoaded  = false;
static uint8_t s_tfIdx          = 0;

static SemaphoreHandle_t s_mutex = nullptr;

// Set by the display widget (slot 0) so the status widget (slot 1) skips the
// timeframe cycle on screen-entry  only a real touch should cycle it.
static bool s_screenJustEntered = false;

// Re-render signal: increment to request a repaint. Written from bg task and
// onActivate() (32-bit writes are atomic on ESP32).
static volatile uint32_t s_renderGen = 1;

static int s_lastMin  = -1;
static int s_lastHour = -1;
static int s_lastDay  = -1;
static int s_lastWeek = -1;

// === Helpers ===

// Format price: full integer part, up to 'digits' decimals, max 10 total chars.
static void formatPrice(float price, uint8_t digits, char* buf, size_t n) {
    if (digits == 0 || price == 0.0f) {
        snprintf(buf, n, "%.0f", price);
        return;
    }
    snprintf(buf, n, "%.*f", (int)digits, price);

    // Trim DECIMALS only (from the right) until strlen <= 10  never the integer
    // part, since dropping integer digits would show a wrong price.
    int len = (int)strlen(buf);
    if (strchr(buf, '.')) {
        while (len > 10 && buf[len - 1] != '.') buf[--len] = '\0';
        if (len > 0 && buf[len - 1] == '.') buf[--len] = '\0';  // remove dangling decimal point
    }
}

static inline void markAllDirty() {
    s_renderGen++;
}

// Time-bucket indices derived from a Unix timestamp, used to detect
// minute/hour/day/week rollovers for the periodic refresh logic below.
static inline int minuteOf(time_t t) { return (int)((t / 60) % 60); }
static inline int hourOf(time_t t)   { return (int)((t / 3600) % 24); }
static inline int dayOf(time_t t)    { return (int)(t / 86400); }
static inline int weekOf(time_t t)   { return (int)(t / (7 * 86400)); }

// === Price alerts ===
// dc.crypto_alerts = "SYM<val;SYM>val"; '?' (any non <,> op) = inactive.
// Edge-triggered: the first eval baselines from the current price, so a threshold
// already crossed at boot won't fire only a live crossing does.
// One-shot: on firing, the op char is rewritten to '?' in the RAM config (not
// flashed → reboot re-arms) so oscillation around the threshold can't re-trigger.
constexpr uint8_t MAX_ALERTS = 12;

struct PriceAlert { uint8_t asset; char op; float threshold; };
static PriceAlert    s_alerts[MAX_ALERTS];
static uint8_t       s_numAlerts               = 0;
static bool          s_alertFired[MAX_ALERTS]  = {};   // true = triggered, waiting to re-arm
static bool          s_alertLit[MAX_ALERTS]    = {};   // fired via a live crossing -> highlight
static bool          s_alertDone[MAX_ALERTS]   = {};   // one-shot consumed -> never re-fires until reload
static int16_t       s_alertOpPos[MAX_ALERTS]  = {};   // byte offset of the op char in dc.crypto_alerts
static bool          g_alertActive[NUM_ASSETS] = {};   // any lit alert for this asset (highlight)
static bool          s_alertBaselined          = false;
static volatile bool s_alertPending            = false; // set in bg task, consumed by main loop

static bool ieq(const char* a, const char* b) {
    for (; *a && *b; ++a, ++b) {
        char ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? (char)(*b + 32) : *b;
        if (ca != cb) return false;
    }
    return *a == *b;
}

// Parse dc.crypto_alerts into s_alerts (skips '?'/unknown ops and unknown symbols).
static void parseAlerts() {
    s_numAlerts      = 0;
    s_alertBaselined = false;
    for (uint8_t a = 0; a < NUM_ASSETS; a++) g_alertActive[a] = false;
    for (uint8_t i = 0; i < MAX_ALERTS; i++) {
        s_alertFired[i] = false; s_alertLit[i] = false; s_alertDone[i] = false;
    }

    char buf[sizeof(dc.crypto_alerts)];
    snprintf(buf, sizeof(buf), "%s", dc.crypto_alerts);
    char* save = nullptr;
    for (char* tok = strtok_r(buf, ";", &save); tok && s_numAlerts < MAX_ALERTS;
         tok = strtok_r(nullptr, ";", &save)) {
        while (*tok == ' ') tok++;
        char* opp = strpbrk(tok, "<>?");
        if (!opp) continue;
        char op = *opp;
        if (op != '<' && op != '>') continue;          // '?' = inactive
        // buf mirrors dc.crypto_alerts byte-for-byte → same offset for write-back.
        int16_t opPos = (int16_t)(opp - buf);
        *opp = '\0';
        float thr = (float)atof(opp + 1);
        for (uint8_t a = 0; a < s_numAssets; a++) {
            if (ieq(tok, s_assets[a].name)) {
                s_alertOpPos[s_numAlerts] = opPos;
                s_alerts[s_numAlerts++]   = { a, op, thr };
                LOG_SINFO("[crypto] alert %s%c%s", s_assets[a].name, op, opp + 1);
                break;
            }
        }
    }
    LOG_SINFO("[crypto] %u price alert(s) active", (unsigned)s_numAlerts);
}

// Edge-triggered evaluation; caller must hold s_mutex. The first pass after a
// (re)parse only baselines: an already-crossed threshold is armed silently, and
// fires + highlights only on a later live crossing (re-arms when it crosses back).
static void evaluateAlertsCore() {
    bool newlyFired = false;
    for (uint8_t i = 0; i < s_numAlerts; i++) {
        if (s_alertDone[i]) continue;                   // one-shot already consumed
        float p = g_currentPrice[s_alerts[i].asset];
        if (p <= 0.0f) continue;                        // no price yet
        bool met = (s_alerts[i].op == '<') ? (p < s_alerts[i].threshold)
                                           : (p > s_alerts[i].threshold);
        if (!s_alertBaselined) {                        // baseline: consume, no highlight
            s_alertFired[i] = met;
            s_alertLit[i]   = false;
            continue;
        }
        if (met && !s_alertFired[i]) {
            s_alertFired[i] = true;  s_alertLit[i] = true;  newlyFired = true;
            // One-shot: disarm and rewrite the op char to '?' in the RAM config
            // (not flashed → reboot re-arms) so oscillation can't re-trigger it.
            s_alertDone[i] = true;
            int16_t pos = s_alertOpPos[i];
            if (pos >= 0 && pos < (int16_t)sizeof(dc.crypto_alerts) &&
                (dc.crypto_alerts[pos] == '<' || dc.crypto_alerts[pos] == '>')) {
                dc.crypto_alerts[pos] = '?';
            }
        }
        else if (!met) { s_alertFired[i] = false; s_alertLit[i] = false; }
    }
    s_alertBaselined = true;

    // Per-asset highlight = any *lit* (live-crossed) alert for that asset.
    bool changed = false;
    for (uint8_t a = 0; a < s_numAssets; a++) {
        bool act = false;
        for (uint8_t i = 0; i < s_numAlerts; i++)
            if (s_alerts[i].asset == a && s_alertLit[i]) { act = true; break; }
        if (g_alertActive[a] != act) { g_alertActive[a] = act; changed = true; }
    }
    if (changed) markAllDirty();

    // In the bg task only raise a flag; the main loop drives UI/audio so display
    // and buzzer are never touched from two cores at once.
    if (newlyFired) {
        s_alertPending = true;
        LOG_SINFO("[crypto] price alert fired");
    }
}

// Periodic evaluation from the bg task (after prices update).
static void evaluateAlerts() {
    if (s_numAlerts == 0 || !s_mutex) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    evaluateAlertsCore();
    xSemaphoreGive(s_mutex);
}

// WebPrefs callback after the user edits dc.crypto_alerts: re-parse and re-baseline
// against current prices so an already-satisfied rule stays quiet. Applies live.
void cryptoReloadAlerts() {
    if (!s_mutex) { parseAlerts(); return; }   // before the bg task/mutex exist
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    parseAlerts();          // rebuild; baselined=false, all flags cleared
    evaluateAlertsCore();   // baseline from current price: no fire, highlight off
    xSemaphoreGive(s_mutex);
    markAllDirty();
}

// Consumed by the main loop: returns true once after a new price alert fired.
bool cryptoTakeAlert() {
    if (!s_alertPending) return false;
    s_alertPending = false;
    return true;
}

// Ack gesture (short centre tap): clear all live highlights so prices return to
// their normal colour. s_alertDone stays set, so one-shot alerts remain disarmed.
void cryptoClearAlertHighlights() {
    if (!s_mutex) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool changed = false;
    for (uint8_t i = 0; i < s_numAlerts; i++) s_alertLit[i] = false;
    for (uint8_t a = 0; a < NUM_ASSETS; a++)
        if (g_alertActive[a]) { g_alertActive[a] = false; changed = true; }
    xSemaphoreGive(s_mutex);
    if (changed) markAllDirty();
}

// === Render Helpers ===

// Returns the configured palette color, or COLOR_NONE if unset (0 = "use default").
static inline uint8_t colorOrDefault(uint16_t configured) {
    return (configured > 0 && configured < 32) ? (uint8_t)configured : Display::COLOR_NONE;
}

// Pixel layout per asset row (320 px wide), font2x3 = 16×24 px/char:
//   x=0 name (5 ch=80) | x=80 price (11 ch=176) | x=256 change% (font1x2, 8 ch=64)
static void renderAssetRows(int xPx, int yPx) {
    // Snapshot shared data under mutex.
    float   prices[NUM_ASSETS];
    bool    stale[NUM_ASSETS];
    bool    alerted[NUM_ASSETS];
    float   refPrice[NUM_ASSETS];
    uint8_t tfIdx;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    tfIdx = s_tfIdx;
    memcpy(prices,  g_currentPrice, sizeof(prices));
    memcpy(stale,   g_stale,        sizeof(stale));
    memcpy(alerted, g_alertActive,  sizeof(alerted));
    uint8_t  bucket = s_timeframes[tfIdx].bucket;
    uint16_t offset = s_timeframes[tfIdx].offset;
    // Offset must stay within the allocated depth; out-of-range → show no ref
    // price (0) instead of reading past the buffer.
    bool     offsetValid = (offset < HISTORY_DEPTH);
    for (uint8_t a = 0; a < s_numAssets; a++)
        refPrice[a] = (g_history && offsetValid) ? g_history[a][bucket][offset] : 0.0f;
    xSemaphoreGive(s_mutex);

    for (uint8_t a = 0; a < s_numAssets; a++) {
        int rowY = yPx + (int)a * 28;  // wartrisFont2x3 charH = 24 px

        // Name field (always 5 chars: 4 padded + 1 space separator)
        char nameBuf[6];
        snprintf(nameBuf, sizeof(nameBuf), "%-4s ", s_assets[a].name);
        uint8_t symCol = colorOrDefault(dc.symbol_col);
        display.drawPixelText(wartrisFont2x3, nameBuf,
                              xPx, rowY, Display::DrawModeBgFill, symCol);

        // Price field (10 price chars + 1 space separator = 11 chars)
        char priceTmp[12] = "===";
        if (prices[a] > 0.0f)
            formatPrice(prices[a], s_assets[a].digits, priceTmp, sizeof(priceTmp));

        char priceFull[13];
        snprintf(priceFull, sizeof(priceFull), "%-10s ", priceTmp);  // left-align + sep

        uint8_t priceColor = alerted[a] ? colorOrDefault(dc.crypto_alert_col)
                            : (stale[a] ? grey1 : colorOrDefault(dc.price_col));
        display.drawPixelText(wartrisFont2x3, priceFull,
                              xPx + 80, rowY, Display::DrawModeBgFill, priceColor);

        // Change-% field (8 chars, wartrisFont1x2, vertically centered in 24 px row)
        char pctFull[9];
        memset(pctFull, ' ', 8);
        pctFull[8] = '\0';

        uint8_t pctColor = grey1;
        if (prices[a] > 0.0f && refPrice[a] > 0.0f) {
            float chg = ((prices[a] / refPrice[a]) - 1.0f) * 100.0f;
            char  tmp[10];
            snprintf(tmp, sizeof(tmp), "%+.1f%%", chg);
            int   tlen = (int)strlen(tmp);
            if (tlen > 8) tlen = 8;
            memcpy(pctFull, tmp, tlen);
            pctColor = (chg >= 0.0f) ? green2 : red2;
        }
        display.drawPixelText(wartrisFont1x2, pctFull,
                              xPx + 256, rowY + 4,  // +4 px vertical centering in 24 px row
                              Display::DrawModeBgFill, pctColor);
    }
}

// Status row uses wartrisFont1x2 (8×16 px).
// Centered vertically in the 96 px slot (y offset = (96-16)/2 = 40 px).
static void renderStatusRow(int xPx, int yPx) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    uint8_t tfIdx = s_tfIdx;
    xSemaphoreGive(s_mutex);

    uint8_t dtCol = colorOrDefault(dc.date_time_col);
    int rowY = yPx + 50;

    // Timeframe left-aligned, fixed 5 chars so shorter labels erase longer ones
    char tfBuf[8];
    snprintf(tfBuf, sizeof(tfBuf), "[%s]%s", s_timeframes[tfIdx].label,
             strlen(s_timeframes[tfIdx].label) < 3 ? " " : "");
    display.drawPixelText(wartrisFont2x3, tfBuf, xPx, rowY, Display::DrawModeBgFill, dtCol);

    // Date/time  right-aligned: 12 chars × 16 px = 192 px from right edge (320 px)
    char dtBuf[14];
    String dt = tm.getFormattedTimestamp(dc.clock_24h ? "%d.%b %H:%M" : "%d.%b %I:%M");
    dt.toUpperCase();
    snprintf(dtBuf, sizeof(dtBuf), "%s", dt.c_str());
    display.drawPixelText(wartrisFont2x3, dtBuf, (320-8) - 12 * 16, rowY, Display::DrawModeBgFill, dtCol);
}

// === Streaming Klines Parser ===
// Stream sink for the body reader: extracts the close price (field 4) from each
// kline sub-array and stores them oldest-first in out[0..count-1].
// Binance kline = [openTime,"open","high","low","close",...]; field 4 = 4th comma
// at depth 2.
namespace {
class KlineParser : public Stream {
    float*   _out;
    uint16_t _cap;      // capacity (renamed: _max conflicts with system macro)
    int      _depth   = 0;
    int      _commas  = 0;
    bool     _inStr   = false;
    bool     _esc     = false;
    bool     _capture = false;
    char     _tok[20];
    int      _tokLen  = 0;
public:
    int count = 0;

    KlineParser(float* out, uint16_t maxBars) : _out(out), _cap(maxBars) {}

    size_t write(uint8_t b) override {
        char c = (char)b;
        if (_inStr) {
            if (_esc) { _esc = false; if (_capture && _tokLen < 18) _tok[_tokLen++] = c; return 1; }
            if (c == '\\') { _esc = true; return 1; }
            if (c == '"') {
                _inStr = false;
                if (_capture && _tokLen > 0 && count < _cap) {
                    _tok[_tokLen] = '\0';
                    _out[count++] = atof(_tok);
                    _capture = false;
                }
                _tokLen = 0;
                return 1;
            }
            if (_capture && _tokLen < 18) _tok[_tokLen++] = c;
            return 1;
        }
        switch (c) {
            case '[': ++_depth; if (_depth == 2) _commas = 0; break;
            case ']': --_depth; break;
            case ',': if (_depth == 2) ++_commas; break;
            case '"':
                _inStr = true; _tokLen = 0;
                _capture = (_depth == 2 && _commas == 4);
                break;
            default: break;
        }
        return 1;
    }

    size_t write(const uint8_t* buf, size_t n) override {
        for (size_t i = 0; i < n; i++) write(buf[i]);
        return n;
    }

    // Stream read interface  not used, required by abstract base.
    int available() override { return 0; }
    int read()      override { return -1; }
    int peek()      override { return -1; }
};
}  // namespace

// === Streaming Ticker Parser ===
// Parses the symbol-less /ticker/price array [{"symbol":..,"price":..}, ...]:
// captures each "symbol", and if it matches an asset stores the next "price".
namespace {
class TickerParser : public Stream {
    bool   _inStr    = false;
    bool   _esc      = false;
    char   _key[8]   = {};
    char   _tok[20]  = {};
    int    _tokLen   = 0;
    int8_t _matchIdx = -1;

    // Output
    float*  _prices;
    bool*   _found;
    uint8_t _numAssets;

    // Called when a quoted string ends.
    // _afterColon tracks whether we just saw ':' distinguishes key from value.
    bool _afterColon = false;

    void onString() {
        if (!_afterColon) {
            // This was a key - remember it, wait for ':'
            int n = _tokLen < 7 ? _tokLen : 7;
            memcpy(_key, _tok, n); _key[n] = '\0';
        } else {
            // This was a value
            _afterColon = false;
            if (strcmp(_key, "symbol") == 0) {
                _matchIdx = -1;
                for (uint8_t a = 0; a < _numAssets; a++)
                    if (strcmp(_tok, s_assets[a].symbol) == 0) { _matchIdx = a; break; }
            } else if (strcmp(_key, "price") == 0 && _matchIdx >= 0) {
                float p = atof(_tok);
                if (p > 0.0f) { _prices[_matchIdx] = p; _found[_matchIdx] = true; }
            }
        }
    }

public:
    TickerParser(float* prices, bool* found, uint8_t numAssets)
        : _prices(prices), _found(found), _numAssets(numAssets) {}

    size_t write(uint8_t b) override {
        char c = (char)b;
        if (_inStr) {
            if (_esc) { _esc = false; if (_tokLen < 18) _tok[_tokLen++] = c; return 1; }
            if (c == '\\') { _esc = true; return 1; }
            if (c == '"') {
                _inStr = false;
                _tok[_tokLen] = '\0';
                onString();
                _tokLen = 0;
                return 1;
            }
            if (_tokLen < 18) _tok[_tokLen++] = c;
            return 1;
        }
        if (c == '"')  { _inStr = true; _tokLen = 0; return 1; }
        if (c == ':')  { _afterColon = true; return 1; }
        if (c == '{')  { _matchIdx = -1; _key[0] = '\0'; _afterColon = false; }
        return 1;
    }

    size_t write(const uint8_t* buf, size_t n) override {
        for (size_t i = 0; i < n; i++) write(buf[i]);
        return n;
    }

    int available() override { return 0; }
    int read()      override { return -1; }
    int peek()      override { return -1; }
};
}  // namespace

// === Fetch Functions ===

// Manual bounded read (Binance always sends Content-Length here). Own deadline
// because writeToStream() has none  a stalled socket could block ~15 min and
// hold g_fetchActive (webPrefs' busy flag) the whole time.
constexpr uint32_t FETCH_BODY_TIMEOUT_MS = 10000;

// Min contiguous heap before a NEW TLS handshake (mbedtls needs a big alloc).
// Kept low on purpose: below it we defer (retried later) rather than fail hard,
// but fetches have succeeded well under the old 28 KB guard.
constexpr size_t TLS_MIN_BLOCK = 8000;

static inline bool tlsHeapOk(const char* who) {
    size_t maxBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    if (maxBlock < TLS_MIN_BLOCK) {
        LOG_SWARNING("[crypto] %s: low contiguous heap (max_block=%u < %u) deferring",
                     who, (unsigned)maxBlock, (unsigned)TLS_MIN_BLOCK);
        return false;
    }
    return true;
}

// Keep-alive: configure a client/http pair for connection reuse across a batch.
// One TLS session keeps the 16 KB mbedtls buffers allocated once instead of per
// call  the fragmentation that caused the -32512 handshake failures.
static void setupTlsClient(WiFiClientSecure& client, HTTPClient& http, uint32_t timeoutMs) {
    client.setInsecure();
    client.setHandshakeTimeout(20);   // 20 s for TLS on slow CDN
    http.setReuse(true);
    http.setTimeout(timeoutMs);
}

static bool readBodyWithTimeout(HTTPClient& http, Stream& parser, uint32_t timeoutMs) {
    auto* stream = http.getStreamPtr();
    int remaining = http.getSize();
    uint32_t deadline = millis() + timeoutMs;
    uint8_t buf[256];

    while (http.connected() && remaining != 0) {
        if ((int32_t)(millis() - deadline) >= 0) {
            LOG_SWARNING("[crypto] body read timeout, %d bytes left", remaining);
            return false;
        }
        size_t avail = stream->available();
        if (!avail) { delay(1); continue; }
        size_t n = stream->readBytes(buf, avail > sizeof(buf) ? sizeof(buf) : avail);
        parser.write(buf, n);
        if (remaining > 0) remaining -= (int)n;
    }
    return true;
}

// Fetches current prices per symbol over one keep-alive TLS session. The
// symbol-less batch endpoint (~60 KB) overran the body timeout mid-stream, so
// late assets showed stale; ?symbol=XXX returns a ~60-byte object that reads
// instantly. g_fetchActive is held by the caller across this AND the following
// history-retry loop, so no webPrefs request slips through the gap.
static bool fetchCurrentPrices() {
    if (!tlsHeapOk("price batch")) return false;   // #2: defer if heap too fragmented

    WiFiClientSecure client;
    HTTPClient http;
    setupTlsClient(client, http, 15000);

    float prices[NUM_ASSETS] = {};
    bool  found[NUM_ASSETS]  = {};

    for (uint8_t a = 0; a < s_numAssets; a++) {
        char url[160];
        snprintf(url, sizeof(url), BINANCE_URL_TICKER, s_assets[a].symbol);

        LOG_SDEBUG("[crypto] price GET %s", url);
        if (!http.begin(client, url)) {
            LOG_SERROR("[crypto] price %s: begin failed", s_assets[a].name);
            client.stop();
            continue;
        }
        http.setReuse(true);

        int code = http.GET();
        if (code != 200) {
            LOG_SERROR("[crypto] price %s fail code=%d", s_assets[a].name, code);
            http.end();
            client.stop();   // reset connection so the next symbol re-handshakes cleanly
            continue;
        }

        // Single-object response → pull "price" straight out (no symbol match,
        // order-independent; the old streaming parser missed price-before-symbol).
        String body = http.getString();
        http.end();          // reuse=true → keeps the session for the next symbol

        int pi = body.indexOf("\"price\":\"");
        if (pi >= 0) {
            float p = atof(body.c_str() + pi + 9);   // 9 = strlen("\"price\":\"")
            if (p > 0.0f) { prices[a] = p; found[a] = true; }
        }
        if (!found[a])
            LOG_SWARNING("[crypto] price %s: no price in '%s'", s_assets[a].name, body.c_str());
    }
    client.stop();

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool anyOk = false;
    for (uint8_t a = 0; a < s_numAssets; a++) {
        if (found[a]) {
            g_currentPrice[a] = prices[a];
            g_stale[a]        = false;
            anyOk             = true;
            LOG_SINFO("[crypto] price ok %s=%.4f", s_assets[a].name, prices[a]);
        } else {
            g_stale[a] = true;
            LOG_SERROR("[crypto] price %s: not found in batch", s_assets[a].name);
        }
    }
    xSemaphoreGive(s_mutex);
    return anyOk;
}

// Fetches one (asset, interval) kline bucket over a shared client/http pair so a
// batch reuses one TLS session. The caller owns the client lifecycle and holds
// g_fetchActive across the whole batch (see fetchCurrentPrices). On failure the
// connection is torn down for a fresh handshake; on success it stays open.
static bool fetchHistoryStreaming(HTTPClient& http, WiFiClientSecure& client,
                                  uint8_t asset, uint8_t interval) {
    if (!g_history) return false;

    // Low-heap guard applies only to a NEW handshake; skip it when the keep-alive
    // connection is already open (no big alloc happens on reuse).
    if (!client.connected() && !tlsHeapOk(s_assets[asset].name)) {
        g_fetchOk[asset][interval] = false;
        return false;
    }

    char url[192];
    snprintf(url, sizeof(url), BINANCE_URL_KLINES,
             s_assets[asset].symbol,
             s_intervalStr[interval],
             (unsigned)HISTORY_DEPTH);

    LOG_SDEBUG("[crypto] history GET %s", url);
    if (!client.connected())   // heap only matters before a NEW TLS handshake
        LOG_SDEBUG("[crypto] pre-SSL heap: free=%u max_block=%u",
                   (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                   (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

    if (!http.begin(client, url)) {
        LOG_SERROR("[crypto] history %s/%s: begin failed",
                   s_assets[asset].name, s_intervalStr[interval]);
        client.stop();
        g_fetchOk[asset][interval] = false;
        return false;
    }
    http.setReuse(true);   // keep the TLS session open for the next call in the batch

    LOG_SDEBUG("[crypto] history %s/%s: connecting...",
               s_assets[asset].name, s_intervalStr[interval]);
    uint32_t t0 = millis();
    int code = http.GET();
    LOG_SDEBUG("[crypto] history %s/%s: GET done code=%d in %lu ms",
               s_assets[asset].name, s_intervalStr[interval], code, (unsigned long)(millis() - t0));

    if (code != 200) {
        LOG_SERROR("[crypto] history %s/%s fail code=%d",
                   s_assets[asset].name, s_intervalStr[interval], code);
        http.end();
        client.stop();   // connection state uncertain → force a fresh handshake next call
        g_fetchOk[asset][interval] = false;
        return false;
    }

    LOG_SDEBUG("[crypto] history %s/%s: streaming response...",
               s_assets[asset].name, s_intervalStr[interval]);
    float tmp[HISTORY_DEPTH];
    KlineParser parser(tmp, HISTORY_DEPTH);
    bool bodyOk = readBodyWithTimeout(http, parser, FETCH_BODY_TIMEOUT_MS);

    int n = parser.count;
    if (!bodyOk || n == 0) {
        LOG_SERROR("[crypto] history %s/%s: %s (bars=%d)",
                   s_assets[asset].name, s_intervalStr[interval],
                   bodyOk ? "no bars parsed" : "body read timeout", n);
        http.end();
        client.stop();   // unread bytes left → connection dirty, cannot be reused
        g_fetchOk[asset][interval] = false;
        return false;
    }

    http.end();   // reuse=true → keeps the TLS connection alive for the next call

    // Reverse into g_history: oldest-first (API) → newest-first (local).
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    for (int j = 0; j < (int)HISTORY_DEPTH && j < n; j++)
        g_history[asset][interval][j] = tmp[n - 1 - j];
    xSemaphoreGive(s_mutex);

    LOG_SDEBUG("[crypto] history ok %s/%s  bars=%d",
               s_assets[asset].name, s_intervalStr[interval], n);
    g_fetchOk[asset][interval] = true;
    return true;
}

// Seed g_currentPrice[asset] from history if it has no price yet.
// Tries 1h first, falls back to 1d, then 1w.
// No-op if price is already set or history is unavailable.
static void seedPriceIfMissing(uint8_t asset) {
    if (!g_history) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (g_currentPrice[asset] == 0.0f) {
        float seed = 0.0f;
        for (uint8_t i = 0; i < NUM_INTERVALS && seed == 0.0f; i++)
            seed = g_history[asset][i][0];
        if (seed > 0.0f) {
            g_currentPrice[asset] = seed;
            g_stale[asset] = false;
            LOG_SINFO("[crypto] seed %s=%.4f", s_assets[asset].name, seed);
        }
    }
    xSemaphoreGive(s_mutex);
}

static void fetchAllHistory() {
    int totalCalls = (int)s_numAssets * NUM_INTERVALS;
    LOG_SDEBUG("[crypto] cold-boot history fetch (%d calls, limit=%u)…",
              totalCalls, (unsigned)HISTORY_DEPTH);
    uint32_t t0 = millis();
    int ok = 0;
    g_fetchActive = true;

    // One shared keep-alive TLS session for the whole batch (see setupTlsClient).
    WiFiClientSecure client;
    HTTPClient http;
    setupTlsClient(client, http, 30000);   // 30 s read  100 bars ≈ 24 KB streaming

    for (uint8_t a = 0; a < s_numAssets; a++) {
        for (uint8_t i = 0; i < NUM_INTERVALS; i++) {
            if (fetchHistoryStreaming(http, client, a, i)) {
                ok++;
                if (g_currentPrice[a] == 0.0f) seedPriceIfMissing(a);
                markAllDirty();  // each bucket can be the one the active timeframe needs
            }
            vTaskDelay(pdMS_TO_TICKS(100)); // yield between calls
        }
    }
    client.stop();   // close the shared connection at the end of the batch
    g_fetchActive = false;
    LOG_SINFO("[crypto] history done %d/%d ok in %lu ms",
              ok, totalCalls, (unsigned long)(millis() - t0));
    s_historyLoaded = (ok > 0);
}

// Refreshes one history bucket (interval) for all assets and triggers a re-render.
static void refreshBucket(uint8_t interval) {
    g_fetchActive = true;
    WiFiClientSecure client;
    HTTPClient http;
    setupTlsClient(client, http, 30000);
    for (uint8_t a = 0; a < s_numAssets; a++) fetchHistoryStreaming(http, client, a, interval);
    client.stop();
    g_fetchActive = false;
    markAllDirty();
}

// === Widget Classes ===

class CryptoDisplayWidget : public Widget {
    uint32_t _lastGen = 0;
public:
    const char* name() const override { return "crypto"; }

    void onActivate(int16_t, int16_t) override {
        s_screenJustEntered = true;  // tell status widget: this is a screen entry
        _lastGen = 0;                // force full repaint
    }

    void update() override {
        if (!WiFi.isConnected()) return;
        if (Update.isRunning()) return;

        // Cold-boot: fetch all history on first update tick (blocks ~60–120 s).
        if (!s_historyLoaded) {
            fetchAllHistory();
            // Prime the rollover timestamps so hourly/daily/weekly refreshes don't
            // all fire right after the cold-boot fetch.
            time_t t0 = tm.getLocalTimestamp();
            s_lastMin  = minuteOf(t0);
            s_lastHour = hourOf(t0);
            s_lastDay  = dayOf(t0);
            s_lastWeek = weekOf(t0);
        }

        // Per-minute: price update + retry any history bucket still missing data.
        time_t now = tm.getLocalTimestamp();
        int curMin = minuteOf(now);
        if (curMin != s_lastMin) {
            s_lastMin = curMin;
            bool dirty = false;
            // Hold the busy flag across price fetch AND retry loop (see above).
            g_fetchActive = true;
            if (fetchCurrentPrices()) dirty = true;
            evaluateAlerts();   // check alerts against the fresh prices
            // Retry any bucket whose last fetch failed.
            uint8_t retryCount = 0;
            for (uint8_t a = 0; a < s_numAssets; a++)
                for (uint8_t i = 0; i < NUM_INTERVALS; i++)
                    if (!g_fetchOk[a][i]) retryCount++;
            if (retryCount > 0) {
                LOG_SINFO("[crypto] retry loop: %d buckets pending", (int)retryCount);
                uint32_t retryT0 = millis();
                WiFiClientSecure client;
                HTTPClient http;
                setupTlsClient(client, http, 30000);
                for (uint8_t a = 0; a < s_numAssets; a++)
                    for (uint8_t i = 0; i < NUM_INTERVALS; i++)
                        if (!g_fetchOk[a][i]) {
                            LOG_SDEBUG("[crypto] retry %s/bucket%d", s_assets[a].name, i);
                            if (fetchHistoryStreaming(http, client, a, i)) {
                                seedPriceIfMissing(a);
                                dirty = true;
                            }
                        }
                client.stop();
                LOG_SINFO("[crypto] retry loop done in %lu ms", (unsigned long)(millis() - retryT0));
            }
            g_fetchActive = false;
            if (dirty) markAllDirty();
        }

        // Hourly: refresh 1 h history bucket.
        int curHour = hourOf(now);
        if (s_historyLoaded && curHour != s_lastHour) {
            s_lastHour = curHour;
            refreshBucket(0);
        }

        // Daily: refresh 1 d history bucket.
        int curDay = dayOf(now);
        if (s_historyLoaded && curDay != s_lastDay) {
            s_lastDay = curDay;
            refreshBucket(1);
        }

        // Weekly: refresh 1 w history bucket.
        int curWeek = weekOf(now);
        if (s_historyLoaded && curWeek != s_lastWeek) {
            s_lastWeek = curWeek;
            refreshBucket(2);
        }
    }

    void render(uint8_t x, uint8_t y) override {
        if (_lastGen == s_renderGen) return;
        _lastGen = s_renderGen;
        renderAssetRows((int)x * Display::BLOCK_PX, (int)y * Display::BLOCK_PX);
    }
};

class CryptoStatusWidget : public Widget {
    uint32_t _lastGen = 0;
    int      _lastMin = -1;   // tracks minute independently for clock update
public:
    const char* name() const override { return "crypto-status"; }

    void onActivate(int16_t, int16_t) override {
        // On screen entry slot 0 sets s_screenJustEntered first → skip the cycle,
        // just repaint. A real slot touch arrives without the flag → advance.
        if (s_screenJustEntered) {
            s_screenJustEntered = false;
            _lastGen = 0;
            return;
        }
        // Real touch on this slot → advance timeframe.
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_tfIdx = (s_tfIdx + 1) % NUM_TIMEFRAMES;
        uint8_t newTf = s_tfIdx;
        xSemaphoreGive(s_mutex);
        markAllDirty();
        LOG_SDEBUG("[crypto] timeframe -> %s", s_timeframes[newTf].label);
    }

    void render(uint8_t x, uint8_t y) override {
        int curMin = minuteOf(tm.getLocalTimestamp());
        if (_lastGen == s_renderGen && curMin == _lastMin) return;
        _lastGen = s_renderGen;
        _lastMin = curMin;
        renderStatusRow((int)x * Display::BLOCK_PX, (int)y * Display::BLOCK_PX);
    }
};

// === Public Instances ===

static CryptoDisplayWidget _widget_display;
static CryptoStatusWidget  _widget_status;

Widget& widget_crypto        = _widget_display;
Widget& widget_crypto_status = _widget_status;

// Hardcoded fallback used when dc.crypto_assets is missing or invalid.
static const AssetDef kDefaultAssets[NUM_ASSETS] = {
    {"BTC",  "BTCUSDT", 2},
    {"ETH",  "ETHUSDT", 2},
    {"SILV", "XAGUSDT", 2},
    {"GOLD", "XAUUSDT", 2},
    {"SPY",  "SPYUSDT", 2},
    {"QQQ",  "QQQUSDT", 2},
};

// Format: "NAME,SYMBOL,DIGITS;NAME,SYMBOL,DIGITS;..."
static void loadAssets() {
    s_numAssets = 0;
    char buf[sizeof(dc.crypto_assets)];
    strlcpy(buf, dc.crypto_assets, sizeof(buf));
    char* saveptr;
    char* entry = strtok_r(buf, ";", &saveptr);
    while (entry && s_numAssets < NUM_ASSETS) {
        char* fields[3] = {};
        char* fptr;
        fields[0] = strtok_r(entry, ",", &fptr);
        fields[1] = strtok_r(nullptr, ",", &fptr);
        fields[2] = strtok_r(nullptr, ",", &fptr);
        if (fields[0] && fields[1] && fields[0][0] && fields[1][0]) {
            strlcpy(s_assets[s_numAssets].name,   fields[0], sizeof(s_assets[0].name));
            strlcpy(s_assets[s_numAssets].symbol, fields[1], sizeof(s_assets[0].symbol));
            s_assets[s_numAssets].digits = fields[2] ? (uint8_t)atoi(fields[2]) : 2;
            s_numAssets++;
        }
        entry = strtok_r(nullptr, ";", &saveptr);
    }
    if (s_numAssets < 1) {
        s_numAssets = NUM_ASSETS;
        memcpy(s_assets, kDefaultAssets, sizeof(kDefaultAssets));
        LOG_SERROR("[crypto] crypto_assets invalid, using defaults");
    }
    LOG_SINFO("[crypto] %d assets loaded", s_numAssets);
}

void cryptoInit() {
    s_mutex = xSemaphoreCreateMutex();

    // Load asset list from WebPrefs config (with JSON validation + fallback).
    loadAssets();

    // Parse the price-alert list (needs the asset names from loadAssets()).
    parseAlerts();

    // Allocate history buffer on heap (calloc zero-initialises it).
    if (!g_history) {
        g_history = (float (*)[NUM_INTERVALS][HISTORY_DEPTH])
                    calloc(NUM_ASSETS, sizeof(*g_history));
        if (!g_history) {
            LOG_SERROR("[crypto] history alloc failed!");
        }
    }

    memset(g_currentPrice, 0, sizeof(g_currentPrice));
    memset(g_stale,    false, sizeof(g_stale));
    memset(g_fetchOk,  false, sizeof(g_fetchOk));
    s_historyLoaded = false;
    s_tfIdx         = (dc.init_timeframe >= 1 && dc.init_timeframe <= NUM_TIMEFRAMES)
                          ? (uint8_t)(dc.init_timeframe - 1) : 0;
    s_renderGen     = 1;
    s_lastMin = s_lastHour = s_lastDay = s_lastWeek = -1;
    LOG_SDEBUG("[crypto] init assets=%d history=%p", s_numAssets, (void*)g_history);
}
