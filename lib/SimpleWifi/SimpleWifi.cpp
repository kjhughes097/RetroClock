#include "SimpleWifi.h"

// Init and generate AP SSID
SimpleWifi::SimpleWifi(const char *ap_prefix, bool persistent) {
#if defined(ESP8266)
    mac = ESP.getChipId();
#elif defined(ESP32)
    uint64_t _mac = ESP.getEfuseMac();
    uint8_t * __mac = (uint8_t *)&_mac;
    mac = (__mac[5] | (__mac[4] << 8) | (__mac[3] << 16));
#endif
    LOG_SINFO("[SimpleWifi] MAC: %06X", mac);

    snprintf(ap_ssid, 33, "%s%06X", ap_prefix, mac);
    WiFi.persistent(persistent);
    mode = WIFI_OFF;
    WiFi.mode(WIFI_OFF);
    status = WIFI_DISABLED;
    connection_attempts = 0;
}


void SimpleWifi::setMode(uint16_t wifi_mode) {
    mode = wifi_mode;
    last_attempt_event = 0;
    connection_attempts = 0;
    LOG_SDEBUG("[SimpleWifi] setMode: %d",wifi_mode);
}

// init WiFI as access point
SimpleWifi::init_status SimpleWifi::configAccessPoint(const char *ssid, const char *passwd, int channel, const char * ip, const char *gateway, const char *subnetmask, int hidden, int max_connections) {
    
    ap_passwd = passwd;
    ap_channel = channel;  
    ap_hidden = hidden;
    ap_max_connections = max_connections;
    status = FAILED;
    
    if (ssid != nullptr) {
        snprintf(ap_ssid, sizeof(ap_ssid), "%s", ssid);        
    }
    LOG_SINFO("[SimpleWifi]  Config WIFI AP / SSID: %s", ap_ssid);

    if(!ap_ip.fromString(ip)) {
        ap_ip.fromString(SW_DEFAULT_AP_IP);
        LOG_SWARNING("[SimpleWifi] Invalid or no IP specified! Set to %s",SW_DEFAULT_AP_IP);
    }
    
    if(!ap_gateway.fromString(gateway)) {
        ap_gateway = ap_ip;
        LOG_SWARNING("[SimpleWifi] Invalid or no Gateway specified! Set to %s",ap_gateway.toString().c_str());
    }

    if(!ap_subnetmask.fromString(subnetmask)) {            
        ap_subnetmask = IPAddress(255,255,255,0);
        LOG_SWARNING("[SimpleWifi] Invalid or no Subnetmask specified! Set to 255.255.255.0");
    }

    last_attempt_event = 0;
    status = INIT_DONE;
    return status;   
}

// init WiFI as station, DHCP or Static IP
SimpleWifi::init_status SimpleWifi::configStation(const char *ssid, const char *passwd, int channel, bool use_staticip, const char *ip, const char *gateway, const char *subnetmask, const char *dns1, const char *dns2) {    
    LOG_SINFO("[SimpleWifi]  Config WIFI STA / SSID: %s", ssid);

    this->use_staticip = use_staticip;
    sta_passwd = passwd;
    sta_channel = channel;
    sta_ssid = ssid;
    status = FAILED;
    
    if (use_staticip) {        
        LOG_SINFO("[SimpleWifi] Static IP configuration...");
        
        if(!sta_ip.fromString(ip)) {            
            LOG_SERROR("[SimpleWifi] Invalid IP specified!");
            return status;
        }
        LOG_SDEBUG("[SimpleWifi] Gateway: %s", gateway);

        if(!sta_gateway.fromString(gateway)) {
            sta_gateway = sta_ip;
            LOG_SWARNING("[SimpleWifi] Invalid or no Gateway specified! Set to %s",sta_gateway.toString().c_str());
        }
    
        if(!sta_subnetmask.fromString(subnetmask)) {            
            sta_subnetmask = IPAddress(255,255,255,0);
            LOG_SWARNING("[SimpleWifi] Invalid or no Subnetmask specified! Set to 255.255.255.0");
        }
        
        if(!sta_dns1.fromString(dns1)) {
            sta_dns1 = IPAddress(0,0,0,0);
            LOG_SWARNING("[SimpleWifi] Invalid or no DNS1 specified! Set to 0.0.0.0");
        }

        if(!sta_dns2.fromString(dns2)) {
            sta_dns2 = IPAddress(0,0,0,0);
            LOG_SWARNING("[SimpleWifi] Invalid or no DNS2 specified! Set to 0.0.0.0");
        }

        if (!WiFi.config(sta_ip, sta_gateway, sta_subnetmask, sta_dns1)) {
            LOG_SERROR("[SimpleWifi] Failed to set static IP configuration");
            return status;
        }
        LOG_SINFO("[SimpleWifi] Static IP configuration done");
    } else  {
        WiFi.config(0U, 0U, 0U);
    }
    last_attempt_event = 0;
    status = INIT_DONE;
    return status;  
}

// call setMode() before ensureWifiMode()
SimpleWifi::init_status SimpleWifi::ensureWifiMode(ulong interval, int retries) {
    uint16_t current_mode = WiFi.getMode();
    auto ws = WiFi.status();
    
    // handle mode change
    if (current_mode != mode) {
        // clean up previous mode
        if (current_mode == WIFI_STA && ws == WL_CONNECTED) {
            WiFi.disconnect(true);
        } else if (current_mode == WIFI_AP && (ws == WL_NO_SHIELD || ws == WL_IDLE_STATUS || ws == WL_DISCONNECTED)) {
            WiFi.softAPdisconnect(true);
        }
        
        // special case for turning WiFi off
        if (mode == WIFI_OFF) {
            WiFi.mode(WIFI_OFF);
            status = WIFI_DISABLED;
            LOG_SDEBUG("[SimpleWifi] WIFI Disabled!");
            return status;
        }
    }
    
    // if we're already in the correct mode and connected, update status and return
    if (current_mode == mode) {
        bool is_established = (mode == WIFI_OFF) || 
                             (mode == WIFI_STA && ws == WL_CONNECTED) || 
                             (mode == WIFI_AP && (ws == WL_NO_SHIELD || ws == WL_IDLE_STATUS || ws == WL_DISCONNECTED));
        
        if (is_established) {
            if (status == PENDING) {
                if (mode == WIFI_STA) {
                    sta_ip = WiFi.localIP();
                    WiFi.setHostname(ap_ssid);
                    status = STA_ESTABLISHED;
                    LOG_SINFO("[SimpleWifi] Mode %d, IP: %s", mode, sta_ip.toString().c_str());
                } else if (mode == WIFI_AP) {
                    status = AP_ESTABLISHED;
                    LOG_SINFO("[SimpleWifi] Mode %d, IP: %s", mode, ap_ip.toString().c_str());
                }
            }
            connection_attempts = 0;
            return status;
        }
    }

    // handle retry timing for connection attempts
    if (retries == 0) {
        ulong now = millis();
        if (now - last_attempt_event < interval) {
            return status;
        }
        last_attempt_event = now;
    }

    LOG_SDEBUG("[SimpleWifi] Checking WIFI Status %d", WiFi.status());
    
    // init requested mode (STA or AP)
    status = PENDING;
    connection_attempts++;
    
    if (mode == WIFI_STA) {
        // station mode setup
        WiFi.disconnect(true);
        WiFi.mode(WIFI_STA);
        WiFi.setSleep(false);  // iPhone Personal Hotspot fails to associate with modem-sleep clients
        WiFi.begin(sta_ssid, sta_passwd, sta_channel);
        
        // wait for connection with retries
        for (int t = 0; t < retries; t++) {
            if (WiFi.status() == WL_CONNECTED) {
                status = STA_ESTABLISHED;
                sta_ip = WiFi.localIP();
                WiFi.setHostname(ap_ssid);
                Serial.print("\n");
                LOG_SINFO("[SimpleWifi] Mode %d, IP: %s", mode, sta_ip.toString().c_str());
                break;
            }
            Serial.print(".");
            delay(interval);
        }
    } else if (mode == WIFI_AP) {
        // access point mode setup
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_AP);
        WiFi.softAPConfig(ap_ip, ap_gateway, ap_subnetmask);
        
        if (WiFi.softAP(ap_ssid, ap_passwd, ap_channel, ap_hidden, ap_max_connections)) {
            LOG_SDEBUG("[SimpleWifi] ssid %s, passwd %s, channel %d, hidden %d, max_connections %d", 
                      ap_ssid, ap_passwd, ap_channel, ap_hidden, ap_max_connections);
            LOG_SINFO("[SimpleWifi] AP IP address: '%s'", ap_ip.toString().c_str());
            WiFi.setHostname(ap_ssid);
            status = AP_ESTABLISHED;
        } else {
            status = FAILED;
        }
    }

    return status;
}

void SimpleWifi::disableWifi() {
    WiFi.mode(WIFI_OFF);
    mode = WIFI_OFF;
    status = WIFI_DISABLED;
    last_attempt_event = 0;
    connection_attempts = 0;
    LOG_SDEBUG("[SimpleWifi] WIFI disabled !!");
}