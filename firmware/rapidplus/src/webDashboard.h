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

// Bracket a TLS upload (postData_GoogleSheet) with these: Suspend frees the
// dashboard's heap (closes SSE + stops the server) so mbedTLS can allocate;
// Resume brings the dashboard back afterwards. Prevents the -32512 SSL alloc fail.
void dashboardSuspend();
void dashboardResume();

#endif
