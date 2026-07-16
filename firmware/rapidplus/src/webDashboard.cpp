#include "webDashboard.h"
#include <Arduino.h>
#include <WiFi.h>
// Pull the project headers (define.h -> WebServer.h, which sets WEBSERVER_H)
// BEFORE ESPAsyncWebServer.h. Its WebRequestMethod enum is guarded by
// #ifndef WEBSERVER_H, so this avoids the HTTP_GET/HTTP_POST redefinition clash
// with the IDF http_parser.h. (Same ordering as Bluetooth.h, which compiles.)
#include "PIDControl.h"
#include "displayCLD.h"
#include "button.h"
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

extern String id_device; // defined in Bluetooth.cpp

static AsyncWebServer dashServer(80);
static AsyncEventSource dashEvents("/events");
static bool started = false;
static bool apActive = false;      // true when running as SoftAP fallback (no STA)
static bool handlersReady = false; // routes registered once (survive end/begin cycles)
static bool suspended = false;     // paused to free heap for a TLS upload
static uint32_t lastPush = 0;

// millis() until which each button shows its "pressed/bright" state. Set on a
// web /control press so the UI lights the chip for ~1.5 s (mirrors the mock).
// Index: 0=red, 1=blue, 2=white.
static uint32_t btnLit[3] = {0, 0, 0};

static double r1(double v) { return round(v * 10.0) / 10.0; }

// Derive the home-screen status block from the LCD state machine.
static void fillStatus(JsonObject status, e_statuslcd s)
{
  const char *phase = "idle";
  const char *title = "Idle";
  String sub = "Waiting for a run to start.";

  switch (s)
  {
  case epreheating80:
    phase = "heater"; title = "Preheating"; sub = "Warming to 80 C"; break;
  case eheatLysis:
    phase = "heater"; title = "Lysis heating";
    sub = "~" + String(_displayCLD.lysisRemainSec() / 60.0, 1) + " min remaining"; break;
  case eheating67:
  case epreheat67:
    phase = "heater"; title = "Heating"; sub = "Warming to 67 C"; break;
  case ecalibPreheating:
    phase = "heater"; title = "Calibration heating"; sub = "Warming heaters"; break;
  case eoptoreading:
    phase = "amplification"; title = "Amplification";
    sub = "~" + String(_displayCLD.ampRemainSec() / 60.0, 1) + " min remaining"; break;
  case ewaitLysisTube:
    phase = "idle"; title = "Insert lysis tube"; sub = "Waiting for user"; break;
  case ewaitampTube:
    phase = "idle"; title = "Insert amplification tube"; sub = "Waiting for user"; break;
  case escreenFinished:
    phase = "finished"; title = "Run complete"; sub = "Results ready."; break;
  case errprocess:
    phase = "idle"; title = "Error"; sub = "Check the device."; break;
  case escreenStart:
  default:
    break;
  }

  status["phase"] = phase;
  status["title"] = title;
  status["subtitle"] = sub;
}

static String buildHomeJson()
{
  JsonDocument doc;
  doc["device"] = id_device;
  doc["company"] = "Fortebiotech";

  // Zone-ordered, offset-corrected temps (see PIDControl getters):
  //   bottom = {lysis, ampLeft, ampRight}, hotlid = {topLeft, topRight, ambient}
  double *bt = _PIDControl.getBottomTemperature();
  double *tp = _PIDControl.getHotlidTemperature();
  JsonObject temps = doc["temps"].to<JsonObject>();
  temps["lysis"] = r1(bt[0]);
  temps["ampLeft"] = r1(bt[1]);
  temps["ampRight"] = r1(bt[2]);
  temps["topLeft"] = r1(tp[0]);
  temps["topRight"] = r1(tp[1]);

  fillStatus(doc["status"].to<JsonObject>(), _displayCLD.type_infor);

  bool fin = (_displayCLD.type_infor == escreenFinished);
  JsonObject notify = doc["notify"].to<JsonObject>();
  notify["show"] = fin;
  notify["title"] = "Amplification finished";
  notify["subtitle"] = "Results ready - check the device.";

  uint32_t now = millis();
  JsonObject btns = doc["buttons"].to<JsonObject>();
  btns["red"] = now < btnLit[0];
  btns["green"] = now < btnLit[1]; // "green" chip = physical B_BLUE button
  btns["white"] = now < btnLit[2];

  String out;
  serializeJson(doc, out);
  return out;
}

// One live chart point: calibrated fluorescence per channel {"#1":num,...,"#10":num}
// for a completed amplification round. Matches the value the device shows on its
// serial chart: (raw - origin) / slope. idx = completed round index.
static String buildReadingsJson(uint8_t idx)
{
  JsonDocument doc;
  for (int i = 0; i < 10; i++)
  {
    float raw = (float)_sensor6035.sensor67Value[i][idx];
    float slope = _ForteSetting.parameter.slopes[i];
    float cal = (slope != 0.0f) ? (raw - _ForteSetting.parameter.origins[i]) / slope : raw;
    doc[String("#") + String(i + 1)] = r1(cal);
  }
  String out;
  serializeJson(doc, out);
  return out;
}

// GET/POST /control?btn=red|green|white  -> inject a short press + light the chip.
// (green = physical B_BLUE button)
static void controlHandler(AsyncWebServerRequest *req)
{
  if (!req->hasParam("btn"))
  {
    req->send(400, "application/json", "{\"ok\":false,\"error\":\"missing btn\"}");
    return;
  }
  String b = req->getParam("btn")->value();
  int idx = -1;
  e_statusbutton eb = B_RED;
  if (b == "red") { idx = 0; eb = B_RED; }
  else if (b == "green") { idx = 1; eb = B_BLUE; } // "green" chip = physical B_BLUE button
  else if (b == "white") { idx = 2; eb = B_WHITE; }

  if (idx < 0)
  {
    req->send(400, "application/json", "{\"ok\":false,\"error\":\"unknown btn\"}");
    return;
  }
  btnLit[idx] = millis() + 1500;
  // Post to the button event queue; InputTask (core 1) dispatches it safely.
  _buttonManager.postShortPress(eb);
  req->send(200, "application/json", String("{\"ok\":true,\"btn\":\"") + b + "\"}");
}

// STA joined OR the SoftAP fallback is up -> a browser can reach us.
static bool networkUp() { return WiFi.status() == WL_CONNECTED || apActive; }

void dashboardStartAP()
{
  String ap = "RAPID-" + id_device;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap.c_str()); // open AP (no internet -> Process chart won't load Highcharts)
  apActive = true;
  Serial.printf("[dash] SoftAP '%s' at http://%s/ | free=%u maxAlloc=%u\n",
                ap.c_str(), WiFi.softAPIP().toString().c_str(),
                ESP.getFreeHeap(), ESP.getMaxAllocHeap());
}

void dashboardBegin()
{
  if (started)
    return;
  if (!handlersReady) // register routes once; they persist across end()/begin()
  {
    if (!LittleFS.begin())
      Serial.println("[dash] LittleFS mount failed - UI files unavailable");
    dashServer.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
    dashServer.addHandler(&dashEvents);
    dashServer.on("/control", HTTP_ANY, controlHandler);
    dashServer.on("/home", HTTP_GET, [](AsyncWebServerRequest *req)
                  { req->send(200, "application/json", buildHomeJson()); });
    handlersReady = true;
  }

  dashServer.begin();
  started = true;
  IPAddress ip = apActive ? WiFi.softAPIP() : WiFi.localIP();
  Serial.printf("[dash] dashboard on http://%s/ (%s) | free=%u maxAlloc=%u\n",
                ip.toString().c_str(), apActive ? "AP" : "STA",
                ESP.getFreeHeap(), ESP.getMaxAllocHeap());
}

void dashboardLoop()
{
  if (suspended)
    return; // paused for a TLS upload - don't touch WiFi/heap or restart the server
  if (!started)
  {
    // WiFi usually finishes associating AFTER setup()'s short connect wait, so
    // start the server here the moment STA (or the AP fallback) is up.
    if (!networkUp())
      return;
    dashboardBegin();
  }
  uint32_t now = millis();
  if (now - lastPush < 1000)
    return;
  lastPush = now;

  // Heap watch (every 10 s) so you can measure load, esp. in AP mode.
  static uint32_t lastHeap = 0;
  if (now - lastHeap > 10000)
  {
    lastHeap = now;
    Serial.printf("[dash] heap free=%u maxAlloc=%u clients=%u ap=%d\n",
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap(), dashEvents.count(), apActive);
  }

  dashEvents.send(buildHomeJson().c_str(), "home", now);

  // Stream the amplification chart: one "new_readings" point per NEW completed
  // round (avoids duplicate flat points between rounds). Only during eoptoreading.
  static int lastLoop = -1;
  if (_displayCLD.type_infor == eoptoreading)
  {
    uint8_t loop = _sensor6035.getCurrentLoop();
    if (loop > 0 && loop <= 130 && (int)loop != lastLoop)
    {
      lastLoop = loop;
      dashEvents.send(buildReadingsJson(loop - 1).c_str(), "new_readings", now);
    }
  }
  else
  {
    lastLoop = -1; // reset between runs
  }
}

void dashboardEnd()
{
  if (!started)
    return;
  dashServer.end();
  if (apActive)
  {
    WiFi.softAPdisconnect(true);
    apActive = false;
  }
  started = false;
}

// Temporarily free the dashboard's network heap (close SSE clients + stop the
// server) so a TLS upload gets its ~32KB contiguous block. Without this, an open
// SSE socket + AsyncWebServer fragment the heap and mbedTLS fails with -32512.
// dashboardLoop() stays idle while suspended so NetworkTask can't restart it
// mid-upload; dashboardResume() lets it come back on the next tick.
void dashboardSuspend()
{
  suspended = true;
  if (started)
  {
    dashEvents.close(); // drop SSE clients -> free their buffers
    dashServer.end();
    started = false;
  }
}

void dashboardResume()
{
  suspended = false;
}
