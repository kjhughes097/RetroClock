#include "TimeManager.h"

void TimeManager::setNTP(const char* ntp_server, const long gmt_offset, int daylight_offset) {
    localtime_offset = gmt_offset + daylight_offset;
    use_posix_tz = false;
    configTime(gmt_offset, daylight_offset, ntp_server);
}

// NTP + POSIX-TZ (CET/CEST for Vienna "CET-1CEST,M3.5.0/2,M10.5.0/3")
void TimeManager::setNTPWithTimezone(const char* ntp_server, const char* tz_string) {

    localtime_offset = 0;
    use_posix_tz = true;

#ifdef ESP32
    configTzTime(tz_string, ntp_server);
#else
    // ESP8266: set TZ without NTP offsets
    setenv("TZ", tz_string, 1);
    tzset();
    configTime(0, 0, ntp_server);
#endif
}

bool TimeManager::syncNTP(uint32_t timeout) {

    LOG_SDEBUG("[TimeManager] Checking NTP time...");

#ifdef ESP8266
    time_t now = time(nullptr);
    if (now < 8 * 3600 * 2) {
        return false;
    }
    setTimestamp((uint32_t)now);
    LOG_SDEBUG("[TimeManager] NTP time synchronized successfully");
    return true;

#else
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, timeout)) { // 10ms Timeout
        return false;
    }
    time_t now = mktime(&timeinfo);
    setTimestamp((uint32_t)now);
    LOG_SDEBUG("[TimeManager] NTP time synchronized successfully");
    return true;
#endif
}

void TimeManager::setTimestamp(uint32_t unix_timestamp) {
    stored_timestamp = unix_timestamp;
    last_update = millis();
}

void TimeManager::setTimestamp(const char *date_time, bool utc) {

    struct tm t = {0};
    t.tm_isdst = -1;   // let mktime resolve DST from the local TZ (avoids 1 h error)
    sscanf(date_time, "%d-%d-%d %d:%d:%d",
           &t.tm_year, &t.tm_mon, &t.tm_mday,
           &t.tm_hour, &t.tm_min, &t.tm_sec);

    t.tm_year -= 1900;
    t.tm_mon  -= 1;

    char const* prevTZ = getenv("TZ");    // save TZ
    String prev = prevTZ ? String(prevTZ) : String();

    if (utc) {
        setenv("TZ", "UTC0", 1);
        tzset();
    }

    stored_timestamp = mktime(&t);
    LOG_SDEBUG("[TimeManager] Created TS From Date: %u", stored_timestamp);
    last_update = millis();

    if (utc) {
        if (prev.length()) setenv("TZ", prev.c_str(), 1);
        else unsetenv("TZ");
        tzset();
    }
}

ulong TimeManager::getLocalTimestamp() {
    time_t now = getTimestamp();
    if (!use_posix_tz) return (ulong)(now + localtime_offset);

    struct tm tm_local;
    struct tm tm_utc;
        
    localtime_r(&now, &tm_local);
    gmtime_r(&now, &tm_utc);      
    long offset = (long)(mktime(&tm_local) - mktime(&tm_utc));
    
    return (ulong)(now + offset);
}

ulong TimeManager::getTimestamp() {
    updateTimestamp();
    return stored_timestamp;
}

void TimeManager::updateTimestamp() {
    uint32_t now = millis();
    uint32_t elapsed = now - last_update;
    if (elapsed >= 1000) {
        stored_timestamp += elapsed / 1000;
        last_update += (elapsed / 1000) * 1000;
    }
}

int TimeManager::getHour() {
    time_t ts = (time_t)getTimestamp();
    struct tm tm_info;
    localtime_r(&ts, &tm_info);
    return tm_info.tm_hour;
}

int TimeManager::getMinute() {
    time_t ts = (time_t)getTimestamp();
    struct tm tm_info;
    localtime_r(&ts, &tm_info);
    return tm_info.tm_min;
}

int TimeManager::getWeekday() {
    time_t ts = (time_t)getTimestamp();
    struct tm tm_info;
    localtime_r(&ts, &tm_info);
    return tm_info.tm_wday;   // 0 = Sunday .. 6 = Saturday
}

String TimeManager::getFormattedTimestamp() {
    return getFormattedTimestamp("%Y-%m-%d %H:%M:%S");
}

String TimeManager::getFormattedTimestamp(const char * format) {
    time_t ts = getTimestamp();
    struct tm tm_info;
    localtime_r(&ts, &tm_info);
    char buffer[50];
    strftime(buffer, sizeof(buffer), format, &tm_info);
    return String(buffer);
}
