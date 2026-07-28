#include "globals.h"
#include "network.h"
#include "screens.h"
#include <RetroGfx.h>

extern Display   display;
extern PixelFont wartrisFont1x2;

// Enforces the desired WiFi mode, starts the Web UI once WiFi is up, and falls
// back to AP mode if STA fails after dc.ap_fallback attempts.
void handleWiFi() {
    auto wifi_status = sw.ensureWifiMode(dc.wifi_check_sec * 1000); // non-blocking, default every 5000ms

    // Don't let the idle timer advance while a connection is still PENDING.
    if (wifi_status == SimpleWifi::PENDING) {
        wp.resetIdleTime();
    }

    if (wifi_status == SimpleWifi::AP_ESTABLISHED || wifi_status == SimpleWifi::STA_ESTABLISHED) {
        // Start Web Interface
        wp.start();
        // check for AP fallback condition
    } else if (sw.getMode() == WIFI_STA && dc.ap_fallback > 0 && sw.getAttempts() >= dc.ap_fallback) {
        LOG_SDEBUG("[network] AP Fallback activated");
        wp.stop();
        sw.setMode(WIFI_AP);
    }
}

// Periodic NTP resync (every NTP_UPDATE_INTERVAL min, or once on first call).
void updateNTP() {
    if(dc.ntp_enabled) {
        if(((TIMENOW - ntp_update_event) > NTP_UPDATE_INTERVAL * 1000 * 60) || ntp_update_event == 0) {
            if(WiFi.isConnected()) {
                ntp_updated = tm.syncNTP();
                if(ntp_updated) {
                    ntp_update_event = TIMENOW;
                }
            }
        }
    }
}

void initWifi(bool touch_restore) {
    LOG_SINFO("[network] === Init WIFI ===");
    // prepare Station
    sw.configStation(
        dc.wifi_ssid,
        dc.sta_wifi_passwd,
        0,
        dc.staticip_enabled,
        dc.ip_address,
        dc.gateway_address,
        dc.subnetmask,
        dc.dns1_address,
        dc.dns2_address);

    // Touch held at boot = restore mode (forces open AP for settings recovery).
    bool restore_mode = touch_restore;

    // prepare Access Point
    sw.configAccessPoint(nullptr, restore_mode ? AP_PASSWD : dc.ap_wifi_passwd, dc.ap_channel);

    // set wifi operation mode
    if (dc.ap_only || restore_mode) {
        sw.setMode(WIFI_AP);
    } else {
        sw.setMode(WIFI_STA);
    }

    // if we are in restore mode disable UI password
    if (restore_mode) {
        wp.setAuthentication(false);
    } else {
        wp.setAuthentication(dc.web_auth, dc.web_user, dc.web_passwd);
    }
    
    // activate wifi
    auto wifi_status = sw.ensureWifiMode(1000, 5); // blocking: 1000ms delay, 5 retries

    // Auto-rescue: if STA was requested but failed (bad credentials, router down),
    // fall back to an open AP so the settings page stays reachable to fix the Wi-Fi.
    if (sw.getMode() == WIFI_STA && wifi_status != SimpleWifi::STA_ESTABLISHED) {
        LOG_SWARNING("[network] STA connect failed -> rescue: switching to AP mode");
        wp.setAuthentication(false);                              // open UI in rescue
        sw.configAccessPoint(nullptr, AP_PASSWD, dc.ap_channel);  // known default AP pw
        sw.setMode(WIFI_AP);
        sw.ensureWifiMode(1000, 5);                              // bring up the AP (blocking)
    }
}

bool drawNetworkInfo() {
    
    display.fillBackground(grey0);
    
    display.drawRectangle(red1, 0,0,  40,1);
    display.drawRectangle(red1, 0,1,  1,28);
    display.drawRectangle(red1, 39,1, 1,28);
    display.drawRectangle(red1, 0,29, 40,1);

    const int xPos = 20; 
    const int yPos = 16; 
    display.drawPixelText(wartrisFont1x2, APP_NAME " V" APP_VERSION BUILD_TAG,
                          xPos, yPos,      Display::DrawModeBgFill, red3);
    display.drawPixelText(wartrisFont1x2, "WEBPREFS V" WEBPREFS_VERSION,
                          xPos, yPos + 20, Display::DrawModeBgFill, red3);

    // Use the ACTUAL Wi-Fi mode, so an auto-rescue AP (after a failed STA) is
    // shown as AP too not just the config-flag cases.
    if (sw.getMode() == WIFI_AP) {
        String ssid = "SSID: " + WiFi.softAPSSID();
        String url  = "HTTP://" + WiFi.softAPIP().toString();

        display.drawPixelText(wartrisFont1x2, "AP MODE (2.4G)", xPos, yPos+20*2, Display::DrawModeBgFill, orange3);
        display.drawPixelText(wartrisFont1x2, ssid.c_str(),  xPos, yPos+20*3, Display::DrawModeBgFill, grey3);
        display.drawPixelText(wartrisFont1x2, url.c_str(),   xPos, yPos+20*4, Display::DrawModeBgFill, cyan2);
    } else {

        String url  = "HTTP://" + WiFi.localIP().toString();
        display.drawPixelText(wartrisFont1x2, "STA MODE (2.4G)",  xPos, yPos+20*2, Display::DrawModeBgFill, green3);
        display.drawPixelText(wartrisFont1x2, url.c_str(),    xPos, yPos+20*3, Display::DrawModeBgFill, grey3);
    }
    display.drawPixelText(wartrisFont1x2, "LAST FIRMWARE ON GITHUB",   xPos, yPos+20*6, Display::DrawModeBgFill, cyan2);
    display.drawPixelText(wartrisFont1x2, "HTTPS://GITHUB.COM/SEIZU",   xPos, yPos+20*7, Display::DrawModeBgFill, cyan2);
     
    return true;
}