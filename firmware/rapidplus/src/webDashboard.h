#ifndef _WEBDASHBOARD_H
#define _WEBDASHBOARD_H
#include <Arduino.h> // for String (dashboardHostname)

// Live web dashboard: an AsyncWebServer that serves the UI baked into the firmware
// (src/webAssets.h, generated from data/) and pushes device state to the browser over
// Server-Sent Events. This replaces the old, never-started sync WebServer chart path.
//
// Nothing here touches a filesystem any more: the UI is in flash and the slot labels are
// in NVS, so firmware.bin is the only artifact a unit needs.
//
// Usage:
//   dashboardBegin()  - call once after WiFi has joined (STA). Registers routes
//                       (/, /events, /control, /home) and starts listening on port 80.
//                       Idempotent.
//   dashboardLoop()   - call frequently from a task; it self-throttles to push
//                       one "home" SSE event per second.
//   dashboardSuspend()/dashboardResume() - the only way the server goes down, and it is
//                       reversible (TLS upload window). There is deliberately no permanent
//                       "end": dashboardEnd() died with the WiFiManager portal, its one caller.
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

// Stable DNS label built from id_device (sanitised, lowercased) -> the dashboard is reachable
// at http://<hostname>.local/ (mDNS) and, on routers that resolve DHCP hostnames, http://<hostname>/
// regardless of which IP DHCP hands out. Used by WiFi.setHostname() (main.cpp) and MDNS.begin().
String dashboardHostname();

// THE SoftAP SSID ("FBT-<id>"), clamped to something 802.11 and the QR encoder can carry.
// dashboardStartAP() raises this name and screen_QR() encodes it - both MUST call this rather
// than rebuild the string, or the QR advertises an AP the machine never brought up.
String dashboardApName();

// Fallback when WiFi (STA) won't connect: bring up a SoftAP ("FBT-<id>") so the
// dashboard is still reachable at http://192.168.4.1/. Call from setup() if STA
// failed; dashboardLoop() then starts the server on the AP. Logs free heap.
void dashboardStartAP();

// Cache the per-slot results (CT_value + P/N/S/E/B) for the Process-tab table.
// Call from screen_Result() once results are computed. Served via GET /slots.
void dashboardSetResults(const float *ct, const char *result);

// Cache the per-slot shape measurements for GET /slots: share, rise width (minutes) and the
// shape flag (0 none, 1 arm A, 2 arm B, 3 both). Called from bResultGet() itself rather than
// from its three callers - screen_Result, the Bluetooth path and POST /reviewlast all run that
// one function, so publishing from inside it is the only arrangement in which the shape numbers
// and the CT/outcome table cannot end up describing different runs. -1 means "not measurable".
void dashboardSetShape(const double *window_rate, const double *rise_width, const uint8_t *flag);

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

// Reboot once the device is idle, instead of right now. The new image is already staged
// in the OTA partition, so waiting costs nothing - restarting mid-run destroys a sample.
// dashboardLoop() performs the restart as soon as the delay has passed AND the device is
// not busy; if a run is in progress it simply keeps waiting.
void dashboardRequestRestart(uint32_t delayMs = 800);

// True when the dashboard is being served from the SoftAP fallback (STA never joined).
// Callers must not touch the STA side then: WiFi.begin() re-enters esp_wifi_set_mode()
// and tears at the AP the browser is on, and STA cannot succeed anyway.
bool dashboardIsAP();

// Ask for the SoftAP to be raised ON PURPOSE (Setting menu -> GREEN -> QR), as opposed to the
// boot fallback that only fires when STA never joined.
//
// This only sets a flag. The switch itself is WiFi.mode(WIFI_AP) and must happen on the task
// that owns the dashboard (NetworkTask, from dashboardLoop) - calling it from InputTask would
// re-enter esp_wifi_set_mode() underneath async_tcp, which is the shape that has hung this
// machine twice (GOTCHA 8/11). Ignored while the device is busy: raising the AP kills STA, and
// doing that mid-run would take the end-of-run upload with it.
//
// It is a ONE-WAY switch - there is no runtime path back to STA - so whoever asked for it is
// expected to arm a reboot when the user leaves the QR screen. dashboardApStartedOnDemand()
// reports whether THIS request is what raised the AP, so the boot fallback (where a reboot
// would just land on the AP again, and on every peek at the QR) is left alone.
void dashboardRequestAP();
bool dashboardApStartedOnDemand();

// TEMPORARY (2026-07-27): log free heap + largest contiguous INTERNAL block at a named
// point. Used to find which boot step splits the big region that mbedTLS needs (GOTCHA 2).
void dashHeapProbe(const char *where);

// The 10 per-slot DISEASE labels, loaded from NVS in dashboardBegin() (loadSlotLabels("names"))
// and rewritten by POST /rename. Exposed because postData_GoogleSheet() sends them as the upload's
// "nameSlot" array. The sample labels are deliberately NOT here: they are web-only.
// Read-only for callers outside webDashboard.cpp - /rename owns the writes.
extern String slotNames[10];

#endif
