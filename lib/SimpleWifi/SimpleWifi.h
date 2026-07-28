#ifndef SIMPLEWIFI_H
#define SIMPLEWIFI_H
#define SIMPLEWIFI_VERSION "0.9.3"
#include "serlog.h"

#define SW_DEFAULT_AP_IP  "10.100.10.1"
#define SW_DEFAULT_STA_IP "192.168.0.1"
// access point prefix
#define AP_SSID_PREFIX "SS"
// default access point password (RESTORE MODE)

#if defined(ESP8266)
  #include <ESP8266WiFi.h>  
#elif defined(ESP32)
  #include <WiFi.h>
  #include <esp_wifi.h>
  #define WIFI_OFF WIFI_MODE_NULL
#endif

class SimpleWifi {
  public:
    enum init_status {
      WIFI_DISABLED = 0,
      INIT_DONE,
      AP_ESTABLISHED,
      STA_ESTABLISHED,
      PENDING,
      FAILED
    };
    
    SimpleWifi(const char *ap_prefix = AP_SSID_PREFIX, bool persistent = false);
    init_status configStation(const char *ssid, const char *passwd, int channel = 0, bool use_staticip = false, const char *ip = SW_DEFAULT_STA_IP, const char *gateway = "", const char *subnetmask = "", const char *dns1 = "", const char *dns2 = "");
    init_status configAccessPoint(const char *ssid, const char *passwd, int channel = 1, const char *ip = SW_DEFAULT_AP_IP, const char *gateway = "", const char *subnetmask = "", int hidden = 0, int max_connections = 4);
    init_status ensureWifiMode(ulong attemptInterval, int retries = 0);
    uint16_t getAttempts() { return connection_attempts; }
    init_status getStatus() { return status; }
    void disableWifi();
    void setMode(uint16_t wifi_mode);
    uint16_t getMode() { return mode; }
    uint32_t getMac() { return mac; }
    
  private:
    uint16_t mode; // AP or STA
    uint32_t mac;
    ulong last_attempt_event;
    uint16_t connection_attempts;
    init_status status;
    
    int ap_hidden;
    int ap_max_connections;
    int ap_channel;
    char ap_ssid[33];
    const char * ap_passwd;
    IPAddress ap_ip;
    IPAddress ap_gateway;
    IPAddress ap_subnetmask;

    bool use_staticip;
    int sta_channel;    
    const char * sta_ssid;
    const char * sta_passwd;    
    IPAddress sta_ip;
    IPAddress sta_gateway;
    IPAddress sta_subnetmask;
    IPAddress sta_dns1;
    IPAddress sta_dns2;
};

#endif // SIMPLEWIFI_H