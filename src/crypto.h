#pragma once
#include "screens.h"

// Initialize shared state and mutex call once in setup() after screensInit().
void cryptoInit();

// Returns true once when a new price alert has fired (set by the crypto bg task).
// The main loop consumes this to wake the display, switch to the crypto screen
// and start the alert sound.
bool cryptoTakeAlert();

// Re-parse dc.crypto_alerts and re-baseline against the current prices. Used as
// the WebPrefs callback on the crypto_alerts field so edits apply instantly
// (already-met rules stay quiet, highlight resets) without a reboot.
void cryptoReloadAlerts();

// Clear all live alert highlights (the red price colour) an acknowledge gesture.
// The main loop calls this on a short centre tap on the crypto screen. The one-shot
// 'done' state is kept, so cleared highlights don't reappear on the next evaluation.
void cryptoClearAlertHighlights();

// widget_crypto        → 6 asset price rows  (slot area: grid y = 0..17)
// widget_crypto_status → status + timeframe   (slot area: grid y = 18..29)
//   Touch on the status slot cycles through 9 timeframes:
//   1H→4H→12H→1D→1W→1M→3M→6M→1Y.
extern Widget& widget_crypto;
extern Widget& widget_crypto_status;
