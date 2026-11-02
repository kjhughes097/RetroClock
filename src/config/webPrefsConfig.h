#ifndef WEBPREFSCONFIG_H
#define WEBPREFSCONFIG_H

#include "WebPrefs.h"
#include "webPrefsMacros.h"

// Uncomment to load the built-in defaults instead of the flash/EEPROM settings.
#define READCONFIG

void cbNTPConfigUpdate();
void cbSetClock();
void cbSetBrightness();
void tickNightMode();
bool isValidTime(const char *s);   // "HH:MM" format validator for alarm_time
bool isValidDays(const char *s);   // 7-char 0/1 mask validator for alarm_days
void cryptoReloadAlerts();         // re-parse+apply crypto_alerts on edit (no reboot)
void cbAlarmSwitch();              // stop the buzzer when the alarm-state switch is turned off

#define VAR_NAME(variable) #variable

// EEPROM prefs data
// --------------------------------------------------------------
struct DeviceConfig {
    // === Framework: Storage Header ===
    char header[4];
    uint16_t check_sum;    // data integrity of the stored config
    uint16_t config_sig;   // signature of the parameter-list layout

    // === Display ===
    
    uint16_t clock_digits_col;
    uint16_t small_temp_col;
    uint16_t big_temp_col;
    uint16_t hourglass_bg_col;
    uint16_t hourglass_fg_col;
    uint16_t symbol_col;
    uint16_t price_col;
    uint16_t date_time_col;
    uint16_t init_screen;
    uint16_t init_timeframe;
    uint16_t brightness;     // display backlight, percent (1-100)
    uint16_t display_off_sec; // auto screen-off after N s of no touch (0 = never, else 10-300)
    uint16_t alarm_sound;     // egg-timer alarm melody: 1..5
    uint16_t timer_flash_sec; // egg-timer: flash the screen for N s at 00:00 (0 = no flash)
    bool     timer_flash_hold; // egg-timer: flash until touched (overrides timer_flash_sec)
    uint16_t volume;          // global master volume 0..100 % alarm, egg-timer & crypto alert (scales each song's own volume)
    uint16_t night_brightness; // brightness % inside the night window (0 = screen off)
    char     night_from[6];    // night window start "HH:MM"
    char     night_to[6];      // night window end   "HH:MM"
    char crypto_assets[100];  // format: "NAME,SYMBOL,DIGITS;..." max 6 assets
    char crypto_alerts[100];  // price alerts "SYMBOL<val;SYMBOL>val"; '?' = inactive
    uint16_t crypto_alert_sound; // own melody 1..N played on a price alert
    uint16_t crypto_alert_col;   // price colour of an asset that is in alert (1..31)
    float latitude;
    float lonlatitude;
    bool flip_screen;
    bool panel_toggle;   // off = use the autodetected panel driver, on = the other one (needs reboot)
    bool clock_24h;
    bool hourglass;      // seconds widget style: true = hourglass, false = numbers
    bool fahrenheit;     // weather temperature unit: true = Fahrenheit, false = Celsius

    // === Alarm clock WebPrefs-only, checked once per minute ===
    // Sound + volume are shared with the egg-timer (alarm_sound / volume).
    char     alarm_time[6];  // alarm time "HH:MM"
    char     alarm_days[8];  // weekday mask Mon..Sun, e.g. "1100000"; all 0 = off


    // === Framework: WiFi ===
    char wifi_ssid[33];
    char sta_wifi_passwd[64];
    bool staticip_enabled;
    char ip_address[16];
    char subnetmask[16];
    char gateway_address[16];
    char dns1_address[16];
    char dns2_address[16];
    bool ap_only;
    char ap_wifi_passwd[64];
    uint16_t ap_channel;
    uint16_t ap_fallback;    // AP fall back mode (after retries) / 0 = disabled
    uint16_t wifi_check_sec; // wifi check interval in seconds

    // === Framework: WebPrefs ===
    bool web_auth;
    char web_user[17];
    char web_passwd[64];
    uint16_t web_idle_timeout;

    // === Framework: NTP/Clock ===
    bool ntp_enabled;
    char ntp_server[128];
    char tz_string[50];
    int32_t gmt_offset;
    uint16_t daylight_offset;

    char __STORAGE_END__;
    // <-- values not stored into flash mem -->
    bool alarm_state;          // runtime: true while the buzzer is sounding; toggle off in WebPrefs to stop
    char info_text[256];       // device information shown in WebPrefs
    char date_time[20];
};
// --------------------------------------------------------------



// default prefs
inline const std::vector<WebPrefs::input_field> input_fields = {
// === Display ===
    
    FIELD_UINT16(   init_screen,        "1",                 1, 4,     nullptr),
    FIELD_CHECKBOX( flip_screen,        "off",                         nullptr),
    FIELD_CHECKBOX( panel_toggle,       "off",                         nullptr),
    FIELD_UINT16(   clock_digits_col,   "3",                 1, 31,    nullptr),
    FIELD_UINT16(   small_temp_col,     "0",                 0, 31,    nullptr),
    FIELD_UINT16(   big_temp_col,       "27",                1, 31,    nullptr),
    FIELD_UINT16(   hourglass_bg_col,   "29",                1, 31,    nullptr),
    FIELD_UINT16(   hourglass_fg_col,   "30",                1, 31,    nullptr),
    FIELD_CHECKBOX( hourglass,          "on",                          nullptr),
    FIELD_CHECKBOX( clock_24h,          "on",                          nullptr),
    FIELD_CHECKBOX( fahrenheit,         "off",                         nullptr),

    FIELD_FLOAT(    latitude,           "48.2087",         -90, 90,    nullptr),
    FIELD_FLOAT(    lonlatitude,        "16.3711",        -180,180,    nullptr),    
    
    FIELD_UINT16(   display_off_sec,    "0",                 0, 300,   nullptr),
    
    FIELD_UINT16(   brightness,         "100",               1, 100,   cbSetBrightness),
    FIELD_UINT16(   night_brightness,   "20",                0, 100,   cbSetBrightness),
    FIELD_STRING_VALID( night_from,     "00:00",             5,        cbSetBrightness, isValidTime),
    FIELD_STRING_VALID( night_to,       "00:00",             5,        cbSetBrightness, isValidTime),

    FIELD_UINT16(   alarm_sound,        "1",                 1, 5,     nullptr),
    FIELD_UINT16(   volume,             "20",                0, 100,   nullptr),
    FIELD_STRING_VALID( alarm_time,     "07:00",             5,        nullptr,         isValidTime),
    FIELD_STRING_VALID( alarm_days,     "0000000",           7,        nullptr,         isValidDays),
    FIELD_UINT16(   timer_flash_sec,    "10",                0, 600,   nullptr),
    FIELD_CHECKBOX( timer_flash_hold,   "off",                         nullptr),
    FIELD_CHECKBOX( alarm_state,        "off",                         cbAlarmSwitch),

    FIELD_UINT16(   init_timeframe,     "1",                 1, 9,     nullptr),
    FIELD_UINT16(   symbol_col,         "10",                0, 31,    nullptr),
    FIELD_UINT16(   price_col,          "11",                0, 31,    nullptr),
    FIELD_UINT16(   date_time_col,      "19",                0, 31,    nullptr),
    FIELD_STRING(   crypto_assets,      "BTC,BTCUSDT,2;ETH,ETHUSDT,2;SILV,XAGUSDT,2;GOLD,XAUUSDT,2;SPY,SPYUSDT,2;QQQ,QQQUSDT,2",
                                                             20,       nullptr),
    FIELD_STRING(   crypto_alerts,      "BTC?50000.12;ETH?20000",                  
                                                             0,        cryptoReloadAlerts),
    FIELD_UINT16(   crypto_alert_sound, "2",                 1, 5,     nullptr),
    FIELD_UINT16(   crypto_alert_col,   "31",                1, 31,    nullptr),

// === Framework: WiFi ===
    FIELD_STRING(   wifi_ssid,        WIFI_SSID,            0,         nullptr),
    FIELD_PASSWORD( sta_wifi_passwd,  WIFI_PASSWD ,         8,         nullptr),
    FIELD_CHECKBOX( ap_only,          "off",                           nullptr),
    FIELD_PASSWORD( ap_wifi_passwd,   AP_PASSWD,            8,         nullptr),
    FIELD_UINT16(   ap_channel,       "1",                  1, 13,     nullptr),
    FIELD_UINT16(   ap_fallback,      "0",                  0, 65535,  nullptr),
    FIELD_UINT16(   wifi_check_sec,   "5",                  5, 65535,  nullptr),

    FIELD_CHECKBOX( staticip_enabled, "off",                           nullptr),
    FIELD_STRING(   ip_address,       "192.168.1.2",        0,         nullptr),
    FIELD_STRING(   subnetmask,       "255.255.255.0",      0,         nullptr),
    FIELD_STRING(   gateway_address,  "192.168.1.1",        0,         nullptr),
    FIELD_STRING(   dns1_address,     "1.1.1.1",            0,         nullptr),
    FIELD_STRING(   dns2_address,     "1.0.0.1",            0,         nullptr),

    //FIELD_CHECKBOX( disable_wifi,     "off",                          nullptr),
    //FIELD_UINT16(   wifi_on_toggle,   "5",                  0, 10,    nullptr),

// === Framework: WebPrefs ===
    FIELD_CHECKBOX( web_auth,         "off",                          nullptr),
    FIELD_STRING(   web_user,         WEB_USER,             3,        nullptr),
    FIELD_PASSWORD( web_passwd,       WEB_PASSWD,           0,        nullptr),
    FIELD_UINT16(   web_idle_timeout, "1",                  1, 60,    nullptr),

// === Framework: NTP/Clock ===
    FIELD_CHECKBOX( ntp_enabled,       "on",                          nullptr),
    FIELD_STRING(   ntp_server,        "pool.ntp.org",         0,     cbNTPConfigUpdate),
    FIELD_INT32(    gmt_offset,        "0",           -43200,  50400, cbNTPConfigUpdate),
    FIELD_UINT16(   daylight_offset,   "0",                 0, 7200,  cbNTPConfigUpdate),
    FIELD_STRING(   tz_string,         "CET-1CEST,M3.5.0/2,M10.5.0/3",
                                                            0,        cbNTPConfigUpdate),
// === Runtime only (not stored in flash) ===
    FIELD_STRING(   info_text,         "",                    0,      nullptr),
    FIELD_STRING(   date_time,         "2000-01-01 00:00:01", 0,      cbSetClock)
 };

 #endif
