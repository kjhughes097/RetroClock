#include "globals.h"
#include "storage.h"


// ===
// WebPrefs lifecycle callbacks
// ===
void cbNTPConfigUpdate() {
    if(*dc.tz_string == '\0') {
        tm.setNTP(dc.ntp_server, dc.gmt_offset, dc.daylight_offset);
    } else {
        tm.setNTPWithTimezone(dc.ntp_server, dc.tz_string);
    }
}

void cbSetClock() {
     LOG_SDEBUG("[storage] Set Clock: %s", dc.date_time);
     // The field shows local wall-clock time (getFormattedTimestamp), so parse it
     // as local, not UTC otherwise a non-zero TZ offset shifts the clock on set.
     tm.setTimestamp(dc.date_time, false);
}

extern Display display;

// True if the current local time is inside the night window. Disabled when the
// two times are equal/invalid or the clock isn't synced yet.
bool isNightNow() {
    int from = parseHHMM(dc.night_from);
    int to   = parseHHMM(dc.night_to);
    if (from < 0 || to < 0 || from == to || !ntp_updated) return false;
    int cur = tm.getHour() * 60 + tm.getMinute();
    return (from < to) ? (cur >= from && cur < to)   // same day
                       : (cur >= from || cur < to);  // wraps past midnight
}

// Apply the time-appropriate base brightness: night brightness inside the
// window (0 = screen off), day brightness (dc.brightness) otherwise.
void applyBrightnessForTime() {
    uint16_t pct = isNightNow() ? dc.night_brightness : dc.brightness;
    if (pct == 0) { display.displayOff(); LOG_SDEBUG("[storage] Brightness: night off"); return; }
    if (pct > 100) pct = 100;
    display.setBrightness((uint8_t)((pct * 255 + 50) / 100));
    LOG_SDEBUG("[storage] Brightness: %u%% (%s)", pct, isNightNow() ? "night" : "day");
}

// WebPrefs callback for any brightness/night field applies immediately.
void cbSetBrightness() {
    applyBrightnessForTime();
}

// Called every loop: applies the base brightness only when the day/night window
// changes (so it never fights the manual off-gesture or auto-off in between).
void tickNightMode() {
    static bool inited   = false;
    static bool wasNight = false;
    bool n = isNightNow();
    if (!inited || n != wasNight) {
        inited   = true;
        wasNight = n;
        applyBrightnessForTime();
    }
}

void cbBeforeResponse() {
    LOG_SDEBUG("[storage] Before response");
    snprintf(dc.date_time, sizeof(dc.date_time), "%s", tm.getFormattedTimestamp().c_str());
    snprintf(dc.info_text, sizeof(dc.info_text), "%s V%s%s WebPrefs V%s Panel %s",
             APP_NAME, APP_VERSION, BUILD_TAG, WEBPREFS_VERSION, display.panelName());
    dc.alarm_state = buzzer.isPlaying();   // reflect live alarm/buzzer state in the switch
}

void cbSave(int params_count) {
    // to reduce write cycles, only multiple parameters are written, updates of a single parameter are ignored.
    if (params_count > 1) {
        if (writeConfig()) {
            LOG_SINFO("[storage] Config written! ChkSum: 0x%X", dc.check_sum);
        } else {
            LOG_SERROR("[storage] Save config failed!");
        }
    }
}

void cbDone() {
    LOG_SINFO("[storage] reboot");
    delay(10);
    ESP.restart();
}



// Signature of the parameter-list layout (not values): changes only when a field
// is added/removed/reordered/resized, so a plain version update keeps settings.
static uint16_t configLayoutSignature() {
    uint16_t sig = (uint16_t)input_fields.size();
    for (const auto &f : input_fields) {
        sig = sig * 31 + f.type;
        sig = sig * 31 + f.size;
        sig = sig * 31 + f.offset;
        for (const char *p = f.name; p && *p; ++p) sig = sig * 31 + (uint8_t)*p;
        sig += (uint16_t)f.min * 7 + (uint16_t)f.max * 13;
    }
    return sig;
}

// Write the config to EEPROM flash (checksum + layout signature first).
bool writeConfig() {
    uint8_t *buffer = (uint8_t *)&dc;
    size_t size = offsetof(DeviceConfig, __STORAGE_END__);
    const size_t skip = sizeof(dc.header) + sizeof(dc.check_sum) + sizeof(dc.config_sig);
    dc.check_sum  = computeChecksum((buffer + skip), size - skip);
    dc.config_sig = configLayoutSignature();
    snprintf(dc.header, 4, "%s", "RET");
    EEPROM.begin(size);
    for (size_t i = 0; i < size; i++) {
        EEPROM.write(i, buffer[i]);
    }
    bool result = EEPROM.commit();
    EEPROM.end();
    return result;
}

// Read config from EEPROM; false (→ defaults) on bad header/signature/checksum.
bool readConfig() {
    uint8_t *buffer = (uint8_t *)&dc;
    size_t size = offsetof(DeviceConfig, __STORAGE_END__);
    EEPROM.begin(size);
    for (size_t i = 0; i < size; i++) {
        buffer[i] = EEPROM.read(i);
    }
    EEPROM.end();
    const size_t skip = sizeof(dc.header) + sizeof(dc.check_sum) + sizeof(dc.config_sig);
    uint16_t chk_sum = computeChecksum((buffer + skip), size - skip);
    uint16_t sig     = configLayoutSignature();

    if (strncmp(dc.header, "RET\0", 4) != 0) {
        LOG_SERROR("[storage] Config header invalid -> defaults");
        return false;
    }
    if (dc.config_sig != sig) {
        LOG_SWARNING("[storage] Config layout changed (0x%X != 0x%X) -> defaults", dc.config_sig, sig);
        return false;
    }
    if (dc.check_sum != chk_sum) {
        LOG_SWARNING("[storage] Config checksum bad (0x%X != 0x%X) -> defaults", dc.check_sum, chk_sum);
        return false;
    }
    return true;
}

uint16_t computeChecksum(const uint8_t *data, size_t size) {
    uint16_t checksum = size;
    while (size) {
        checksum += *data++ * size--;
    }
    return checksum;
}

// Start the WebPrefs server on port 80 and load the config (defaults on failure).
// Runs before display.begin(), which needs dc.panel_toggle for the panel driver.
void initWebPrefs() {
    LOG_SINFO("[storage] === Init WebPrefs and load config ===");
    wp.begin(80, (void *)&dc, input_fields, cbDone, cbSave, cbBeforeResponse);
    wp.setBusyCheck([]() { return (bool)g_fetchActive; });

    bool valid_config = false;
#if defined READCONFIG
    // read & verify EEPROM config
    if ((valid_config = readConfig())) {
        LOG_SINFO("[storage] Config OK! ChkSum: 0x%X", dc.check_sum);
    } else {
        LOG_SERROR("[storage] config NOK! ChkSum: 0x%X", dc.check_sum);
    }
#endif

    if (!valid_config) {
        loadDefaultConfig();
    } else {
        wp.debugPrintConfig();
    }
}

// Restore mode (touch held at boot) and invalid configs: fall back to the
// built-in defaults. RAM only, nothing is written until the user hits Save.
void loadDefaultConfig() {
    LOG_SINFO("[storage] Set default config");
    wp.setDefaults();
}