#ifndef NETWORK_H
#define NETWORK_H

// NTP re-sync interval in minutes.
#define NTP_UPDATE_INTERVAL 120

void initWifi(bool touch_restore = false);
void handleWiFi();
void updateNTP();
bool drawNetworkInfo();  // returns true while info screen is active

#endif // NETWORK_H
