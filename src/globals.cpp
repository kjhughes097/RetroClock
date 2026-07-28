#include "globals.h"

// === global instances ===
DeviceConfig dc;
TimeManager tm;
SimpleWifi sw;
WebPrefs wp;
MelodyPlayer buzzer;   // shared passive-buzzer melody player (LEDC ch 0)

ulong ntp_update_event = 0;
bool ntp_updated = false;
volatile bool g_fetchActive = false;

int parseHHMM(const char* s) {
    int h = -1, m = -1;
    if (!s || sscanf(s, "%d:%d", &h, &m) != 2) return -1;
    if (h < 0 || h > 23 || m < 0 || m > 59)    return -1;
    return h * 60 + m;
}