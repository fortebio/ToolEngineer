#ifndef _WEBDASHBOARD_H
#define _WEBDASHBOARD_H

// Live web dashboard: an AsyncWebServer that serves the LittleFS UI (uploaded
// from data/) and pushes device state to the browser over Server-Sent Events.
// This replaces the old, never-started sync WebServer chart path.
//
// Usage:
//   dashboardBegin()  - call once after WiFi has joined (STA). Mounts LittleFS,
//                       registers routes (/, /events, /control, /home) and starts
//                       listening on port 80. Idempotent.
//   dashboardLoop()   - call frequently from a task; it self-throttles to push
//                       one "home" SSE event per second.
//   dashboardEnd()    - stop the server (e.g. before the WiFiManager portal).
//
// The "home" event payload matches the client contract in data/script.js:
//   { device, company, temps{lysis,ampLeft,ampRight,topLeft,topRight},
//     status{phase,title,subtitle}, notify{show,title,subtitle},
//     buttons{red,blue,white} }
//
// During amplification (eoptoreading) it also pushes a "new_readings" event
// {"#1":num,...,"#10":num} = calibrated fluorescence per channel, one point per
// completed acquisition round, for the Process chart.
void dashboardBegin();
void dashboardLoop();
void dashboardEnd();

// Fallback when WiFi (STA) won't connect: bring up a SoftAP ("RAPID-<id>") so the
// dashboard is still reachable at http://192.168.4.1/. Call from setup() if STA
// failed; dashboardLoop() then starts the server on the AP. Logs free heap.
void dashboardStartAP();

// Cache the per-slot results (CT_value + P/N/S/E/B) for the Process-tab table.
// Call from screen_Result() once results are computed. Served via GET /slots.
void dashboardSetResults(const float *ct, const char *result);

// Drop the cached results so GET /slots reports ready=false. Call when a new run
// starts (alongside _sensor6035.clear(), which zeroes lastRunLoops/sensor67Value):
// the table cache (gResultsReady) otherwise outlives the chart data, so /slots would
// keep serving the OLD run's table while /curve returns count=0 (empty chart). Keeping
// them in lockstep means a later Result view sees ready=false and re-loads BOTH from
// EEPROM via POST /reviewlast, instead of a table-with-no-chart desync.
void dashboardClearResults();

// Bracket a TLS upload (postData_GoogleSheet) with these: Suspend frees the
// dashboard's heap (closes SSE + stops the server) so mbedTLS can allocate;
// Resume brings the dashboard back afterwards. Prevents the -32512 SSL alloc fail.
void dashboardSuspend();
void dashboardResume();

// True when the device is running / calibrating / uploading, i.e. settings must not
// change. Allowlist of the few idle states (see isBusy in webDashboard.cpp) - the busy
// set is almost everything, so a denylist would silently leave holes (calib, OTA,
// tube waits all report phase "idle").
// Used in three places: the `busy` flag on the home event (client greys the cards),
// the settings POST handlers (reject with 409), and again inside SettingTask right
// before applying (closes the TOCTOU between the POST and the apply).
bool dashboardDeviceBusy();

// True when the dashboard is being served from the SoftAP fallback (STA never joined).
// Callers must not touch the STA side then: WiFi.begin() re-enters esp_wifi_set_mode()
// and tears at the AP the browser is on, and STA cannot succeed anyway.
bool dashboardIsAP();

#endif
