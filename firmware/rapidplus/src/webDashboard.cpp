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
#include <memory> // shared_ptr: keeps the /curve stream state alive across chunk calls

extern String id_device;          // defined in Bluetooth.cpp
void releaseBluetoothStack(void); // defined in Bluetooth.cpp - frees ~60KB BT memory

static AsyncWebServer dashServer(80);
static AsyncEventSource dashEvents("/events");
static bool started = false;
static bool apActive = false;      // true when running as SoftAP fallback (no STA)
static bool handlersReady = false; // routes registered once (survive end/begin cycles)
static bool suspended = false;     // paused to free heap for a TLS upload
static uint32_t lastPush = 0;

// How long STA gets to associate + get a DHCP lease before we give up and raise the
// SoftAP. Must comfortably exceed a real router's 1-3 s (GOTCHA 5): the fallback is
// one-way (WiFi.mode(WIFI_AP) kills STA until reboot), so being impatient here is far
// more costly than waiting. Nothing blocks meanwhile - the device just has no web yet.
static const uint32_t STA_GRACE_MS = 15000;

// millis() until which each button shows its "pressed/bright" state. Set on a
// web /control press so the UI lights the chip for ~1.5 s (mirrors the mock).
// Index: 0=red, 1=blue, 2=white.
static uint32_t btnLit[3] = {0, 0, 0};

static double r1(double v) { return round(v * 10.0) / 10.0; }

// Is the device doing something that settings must not be changed under?
//
// Written as an ALLOWLIST on purpose: type_infor has ~37 values and the busy set is
// almost all of them, so a denylist silently leaves holes. In particular fillStatus()
// collapses everything unknown to phase "idle", so the calib menus, the OTA screen and
// the tube waits all LOOK idle to the client - locking on `phase` would let a POST land
// mid-calibration (which drives heaters and writes EEPROM from DisplayTask) or mid-run.
static bool isBusy(e_statuslcd s)
{
  switch (s)
  {
  case escreenStart:    // idle start screen
  case escreenFinished: // run done, results on screen
  case escreenReview:   // reviewing results
  case eSettingMenu:    // in the on-device setting menu
  case errprocess:      // error screen; nothing is running
    return false;
  default:
    return true; // running, heating, calibrating, uploading, OTA, tube waits...
  }
}

bool dashboardDeviceBusy() { return isBusy(_displayCLD.type_infor); }

bool dashboardIsAP() { return apActive; }

// Step of the on-device calibration wizard, as a stable name for the web to follow.
// The real flow (see button.cpp / sensor6035.cpp): BLUE long-press -> preheatStart,
// RED -> preheating (both heaters to 55 C, then a 5 minute hold) -> select -> slot ->
// mode -> measure (one BLUE press per tube: 300/200/100/0) -> complete -> save.
static const char *calibStep(e_statuslcd s)
{
  switch (s)
  {
  case ecalibPreheatStart:
    return "preheatStart";
  case ecalibPreheating:
    return "preheating";
  case ecalibSelect:
    return "select";
  case eSelectSlot:
    return "slot";
  case eSelectMode:
    return "mode";
  case eCalibrating:
    return "measure";
  case eWaitingCalib:
    return "measuring";
  case eCalibComplete:
    return "complete";
  case eSaveCalib:
    return "save";
  default:
    return "";
  }
}

// Derive the home-screen status block from the LCD state machine.
static void fillStatus(JsonObject status, e_statuslcd s)
{
  const char *phase = "idle";
  const char *title = "Idle";
  String sub = "Waiting for a run to start.";

  switch (s)
  {
  case epreheating80:
    phase = "heater";
    title = "Heating Lysis";
    sub = "Warming to 80 C";
    break;
  case eheatLysis:
    phase = "heater";
    title = "Lysis heating";
    sub = "~" + String(_displayCLD.lysisRemainSec() / 60.0, 1) + " min remaining";
    break;
  case eheating67:
  case epreheat67:
    phase = "heater";
    title = "Heating";
    sub = "Warming to 67 C";
    break;
  case ecalibPreheating:
    phase = "heater";
    title = "Calibration heating";
    sub = "Warming heaters";
    break;
  case eoptoreading:
    phase = "amplification";
    title = "Amplification";
    sub = "~" + String(_displayCLD.ampRemainSec() / 60.0, 1) + " min remaining";
    break;
  case ewaitLysisTube:
    phase = "idle";
    title = "Insert lysis tube";
    sub = "Waiting for user";
    break;
  case ewaitname:
    // naming gate BEFORE heating: web shows the slot-naming card, Confirm starts preheat
    phase = "waitname";
    title = "Name the samples";
    sub = "Pick a disease per slot, then start";
    break;
  case ewaitampTube:
    // distinct phase so the web can show slot-naming + gate the Start button here
    phase = "waitamp";
    title = "Insert amplification tube";
    sub = "Name slots, then start";
    break;
  case escreenFinished:
    phase = "finished";
    title = "Run complete";
    sub = "Results ready.";
    break;
  case errprocess:
    phase = "idle";
    title = "Error";
    sub = "Check the device.";
    break;
  case escreenStart:
  default:
    break;
  }

  status["phase"] = phase;
  status["title"] = title;
  status["subtitle"] = sub;
  // Explicit busy flag: `phase` is NOT enough for the Setting lock (calib/OTA/tube
  // waits all report "idle"). The client greys the Setting cards on this; the server
  // enforces it independently (a client-side lock alone is worthless - curl or a stale
  // second tab would still POST).
  status["busy"] = isBusy(s);
  // Which step of the on-device calibration wizard we are on (empty = not calibrating).
  // A name, not the raw enum: the client must not hardcode type_infor's numbering.
  status["calib"] = calibStep(s);
}

// What each button DOES in the current screen state (shown on the web chips).
// Mirrors button.cpp handleShortPress_*; "green" chip = physical B_GREEN.
static void fillActions(JsonObject a, e_statuslcd s)
{
  const char *green = "", *red = "", *white = "";
  switch (s)
  {
  case escreenStart:
    green = "Lysis";
    red = "Amplification";
    break; // full flow / amp-only
  case ewaitLysisTube:
    red = "Start lysis";
    white = "Return";
    break;
  case ewaitphase2:
    green = "Amplification";
    white = "Return";
    break;
  case epreheat67:
    green = "Skip preheat";
    white = "Return";
    break;
  case ewaitname:
    red = "Confirm & heat";
    white = "Return";
    break;
  case ewaitampTube:
    red = "Start";
    white = "Return";
    break;
  case escreenFinished:
    red = "Errors Table";
    white = "Next test";
    break;
  case escreenReview:
    red = "Errors Table";
    white = "Return";
    break;
  case eSettingMenu:
    green = "WiFi";
    red = "Upload";
    white = "Back";
    break;
  case eUpdateOTA:
    green = "Dismiss";
    red = "Update";
    break;
  case errprocess:
    break; // buttons frozen on error
  default:
    white = "Return";
    break; // any running screen
  }
  a["green"] = green;
  a["red"] = red;
  a["white"] = white;
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
  btns["green"] = now < btnLit[1]; // "green" chip = physical B_GREEN button
  btns["white"] = now < btnLit[2];

  fillActions(doc["actions"].to<JsonObject>(), _displayCLD.type_infor);

  // Outcome of the last web-queued settings request. POST /config can only ACK that it
  // QUEUED (blocking the AsyncTCP task to wait for the apply is what trips the task
  // watchdog), and SettingTask drops the request if a run started in between - so the
  // client must watch its seq here to know whether the write actually landed.
  JsonObject cfg = doc["cfg"].to<JsonObject>();
  cfg["seq"] = _ForteSetting.cfgSeq;
  const char *cs = "none";
  switch (_ForteSetting.cfgState)
  {
  case ForteSetting::CFG_PENDING:
    cs = "pending";
    break;
  case ForteSetting::CFG_APPLIED:
    cs = "applied";
    break;
  case ForteSetting::CFG_BUSY:
    cs = "busy";
    break;
  default:
    break;
  }
  cfg["state"] = cs;

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
  doc["i"] = idx; // round index -> same x base as /curve (client skips non-"#" keys)
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
// (green = physical B_GREEN button)
static void controlHandler(AsyncWebServerRequest *req)
{
  if (!req->hasParam("btn"))
  {
    req->send(400, "application/json", "{\"ok\":false,\"error\":\"missing btn\"}");
    return;
  }
  String b = req->getParam("btn")->value();

  // Web "Amplification": name the slots BEFORE heating. Enter ewaitname (naming gate)
  // WITHOUT pressing the physical RED - the physical Amplification button heats directly
  // and must keep doing so. Guarded to the idle start screen; type_infor is a 4-byte
  // aligned enum so this atomic write from the AsyncTCP task is safe (same as handleCalib).
  if (b == "ampname")
  {
    if (_displayCLD.type_infor == escreenStart)
    {
      _displayCLD.type_infor = ewaitname;
      _displayCLD.changeScreen = true;
    }
    req->send(200, "application/json", "{\"ok\":true,\"btn\":\"ampname\"}");
    return;
  }

  int idx = -1;
  e_statusbutton eb = B_RED;
  if (b == "red")
  {
    idx = 0;
    eb = B_RED;
  }
  else if (b == "green")
  {
    idx = 1;
    eb = B_GREEN;
  } // "green" chip = physical B_GREEN button
  else if (b == "white")
  {
    idx = 2;
    eb = B_WHITE;
  }

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

// ---- Process tab: slot names (persisted to /slotnames.json) + cached results ----
static String slotNames[10];
static float gCT[10] = {0};
static char gResult[10] = {0};
static bool gResultsReady = false;

static void loadSlotNames()
{
  File f = LittleFS.open("/slotnames.json", "r");
  if (!f)
    return; // no file yet -> names stay empty
  JsonDocument doc;
  if (!deserializeJson(doc, f))
    for (int i = 0; i < 10; i++)
      slotNames[i] = doc[i] | "";
  f.close();
}

static void saveSlotNames()
{
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < 10; i++)
    arr.add(slotNames[i]);
  File f = LittleFS.open("/slotnames.json", "w");
  if (!f)
    return;
  serializeJson(doc, f);
  f.close();
}

// Called from screen_Result() when a run's results are computed.
void dashboardSetResults(const float *ct, const char *result)
{
  for (int i = 0; i < 10; i++)
  {
    gCT[i] = ct[i];
    gResult[i] = result[i];
  }
  gResultsReady = true;
}

// GET /slots -> {ready, slots:[{name, ct, result} x10]}. ct only for P/S (has CT).
static void handleSlots(AsyncWebServerRequest *req)
{
  // While a new run is amplifying, the cached results belong to the PREVIOUS run but
  // /curve already serves the new one - reporting both would let the Result tab show a
  // table and a chart from different runs. The device holds one run, so hide the stale
  // results until this run produces its own (dashboardSetResults from screen_Result).
  bool ready = gResultsReady && (_displayCLD.type_infor != eoptoreading);

  JsonDocument doc;
  doc["ready"] = ready;
  JsonArray arr = doc["slots"].to<JsonArray>();
  for (int i = 0; i < 10; i++)
  {
    JsonObject s = arr.add<JsonObject>();
    s["name"] = slotNames[i];
    if (ready && (gResult[i] == 'P' || gResult[i] == 'S'))
      s["ct"] = r1(gCT[i]);
    else
      s["ct"] = nullptr;
    s["result"] = ready ? String(gResult[i]) : String("");
  }
  String out;
  serializeJson(doc, out);
  req->send(200, "application/json", out);
}

// Incremental writer for GET /curve: emits the payload through a small rolling buffer
// instead of materialising it.
//
// WHY: a full run is amplification_time = 120 rounds x 10 channels = 1200 values.
// Building that as JsonDocument -> serializeJson -> String -> the copy AsyncWebServer
// keeps for the response costs ~30 KB of heap at peak (measured on the device by
// test/test_webcurve). That spike lands exactly at the end of a run, where
// screen_Result() runs postData_GoogleSheet() and mbedTLS needs 32-40 KB CONTIGUOUS
// (GOTCHA 2) -> fragmentation / OOM -> the device reboots after a 40 minute run.
// Streaming keeps the peak at ~1.5 KB (the pending buffer), so /curve can never
// starve the upload.
struct CurveWriter
{
  uint8_t n = 0;
  uint32_t interval = 0;
  int ch = 0;
  int j = 0;
  int stage = 0; // 0 = header, 1 = body, 3 = done
  String pend;

  // Append the next token. Kept token-sized so `pend` stays ~one chunk + one number.
  void step()
  {
    if (stage == 0)
    {
      pend += "{\"count\":";
      pend += n;
      pend += ",\"intervalMs\":";
      pend += interval;
      pend += ",\"series\":[[";
      stage = 1;
      ch = 0;
      j = 0;
      return;
    }
    if (stage == 1)
    {
      if (j < n)
      {
        if (j)
          pend += ",";
        float raw = (float)_sensor6035.sensor67Value[ch][j];
        float s = _ForteSetting.parameter.slopes[ch];
        pend += String(r1((s != 0.0f) ? (raw - _ForteSetting.parameter.origins[ch]) / s : raw), 1);
        j++;
        return;
      }
      pend += "]";
      ch++;
      j = 0;
      if (ch < 10)
      {
        pend += ",[";
        return;
      }
      pend += "]}";
      stage = 3;
    }
  }

  // AsyncWebServer chunked filler: write up to maxLen bytes, return 0 at EOF.
  size_t fill(uint8_t *buf, size_t maxLen)
  {
    while (pend.length() < maxLen && stage != 3)
      step();
    if (stage == 3 && pend.length() == 0)
      return 0; // done - only ever returned once everything has been emitted
    size_t k = pend.length() < maxLen ? pend.length() : maxLen;
    memcpy(buf, pend.c_str(), k);
    pend.remove(0, k);
    return k;
  }
};

// ---- Setting tab: config / wifi / calib ------------------------------------
//
// One /config endpoint serves all the value cards: each card just POSTs the subset of
// keys it owns, because JsonDataConfig() applies only the keys present. The handlers
// here VALIDATE and QUEUE; SettingTask applies (see ForteSetting::drainPending).
//
// Validation is not optional: the firmware range-checks NOTHING. Setpoints, PID gains
// and overheat limits go straight into the control loop and are persisted, and the
// array loops write to fixed C arrays with no bounds check. So this is a trust
// boundary for a heater, not form politeness.

// Reject a settings write while the device is running/calibrating/uploading.
static bool guardBusy(AsyncWebServerRequest *req)
{
  if (!dashboardDeviceBusy())
    return false;
  req->send(409, "application/json",
            "{\"ok\":false,\"error\":\"device busy - cannot change settings now\"}");
  return true;
}

static bool numInRange(JsonVariantConst v, double lo, double hi)
{
  if (!v.is<double>() && !v.is<int>())
    return false;
  double d = v.as<double>();
  return d >= lo && d <= hi;
}

// Every array key writes into a fixed-size C array with no bounds check, so the
// length must match EXACTLY before this reaches JsonDataConfig().
static bool arrayLenIs(JsonVariantConst v, size_t n)
{
  return v.is<JsonArrayConst>() && v.as<JsonArrayConst>().size() == n;
}

// Validate the subset of keys a POST carries. Unknown keys are rejected rather than
// passed through: JsonDataConfig() would happily apply anything it recognises.
static bool validateConfig(JsonObjectConst o, String &err)
{
  for (JsonPairConst kv : o)
  {
    String k = kv.key().c_str();
    JsonVariantConst v = kv.value();

    if (k == "para version" || k == "PCB version")
      continue; // injected/echoed, char[10] - checked below
    else if (k == "device ID" || k == "units")
    {
      if (!v.is<const char *>() || strlen(v.as<const char *>()) > 9)
        return err = k + " must be <= 9 chars (fixed char[10] in EEPROM)", false;
    }
    else if (k == "lysis temperature" || k == "amplification temperature")
    {
      if (!numInRange(v, 20, 110))
        return err = k + " out of range (20..110 C)", false;
    }
    else if (k == "lysis duration")
    {
      if (!numInRange(v, 0, 65535))
        return err = k + " out of range (0..65535 s)", false;
    }
    else if (k == "opto preheat time")
    {
      if (!numInRange(v, 0, 3600))
        return err = k + " out of range (0..3600 s)", false;
    }
    else if (k == "amplification time")
    {
      // Hard cap: COUNTER indexes sensor67Value[10][130]. A bigger value overflows
      // that buffer mid-run.
      if (!numInRange(v, 1, 130))
        return err = k + " out of range (1..130 rounds)", false;
    }
    else if (k == "time per loop")
    {
      if (!numInRange(v, 1000, 120000))
        return err = k + " out of range (1000..120000 ms)", false;
    }
    else if (k == "LED Duration")
    {
      if (!numInRange(v, 0, 10000))
        return err = k + " out of range (0..10000 ms)", false;
    }
    else if (k == "LED power")
    {
      if (!arrayLenIs(v, 10))
        return err = "LED power must be exactly 10 values", false;
      for (JsonVariantConst e : v.as<JsonArrayConst>())
        if (!numInRange(e, 0, 255))
          return err = "LED power values must be 0..255", false;
    }
    else if (k == "PID parameter" || k == "PID2 parameter" || k == "PID3 parameter")
    {
      if (!arrayLenIs(v, 3))
        return err = k + " must be exactly 3 values (Kp,Ki,Kd)", false;
      for (JsonVariantConst e : v.as<JsonArrayConst>())
        if (!numInRange(e, 0, 1000))
          return err = k + " gains must be 0..1000", false;
    }
    else if (k == "Bottom overheat value")
    {
      if (!arrayLenIs(v, 3))
        return err = k + " must be exactly 3 values", false;
      for (JsonVariantConst e : v.as<JsonArrayConst>())
        if (!numInRange(e, 0, 50))
          return err = k + " must be 0..50 C", false;
    }
    else if (k == "Top overheat value")
    {
      if (!arrayLenIs(v, 2))
        return err = k + " must be exactly 2 values", false;
      for (JsonVariantConst e : v.as<JsonArrayConst>())
        if (!numInRange(e, 0, 50))
          return err = k + " must be 0..50 C", false;
    }
    else if (k == "bottom temperature sensor seq" || k == "top temperature sensor seq")
    {
      if (!arrayLenIs(v, 3))
        return err = k + " must be exactly 3 values", false;
      for (JsonVariantConst e : v.as<JsonArrayConst>())
        if (!numInRange(e, 0, 2))
          return err = k + " values must be 0..2", false;
    }
    else if (k == "temperature value calibration")
    {
      if (!arrayLenIs(v, 6))
        return err = k + " must be exactly 6 values", false;
      for (JsonVariantConst e : v.as<JsonArrayConst>())
        if (!numInRange(e, -20, 20))
          return err = k + " offsets must be -20..20 C", false;
    }
    else if (k == "top heater PWM")
    {
      if (!arrayLenIs(v, 2))
        return err = k + " must be 2 rows", false;
      for (JsonVariantConst row : v.as<JsonArrayConst>())
      {
        if (!arrayLenIs(row, 2))
          return err = k + " each row must be [low,high]", false;
        for (JsonVariantConst e : row.as<JsonArrayConst>())
          if (!numInRange(e, 0, 255))
            return err = k + " values must be 0..255", false;
      }
    }
    else if (k == "buzzer")
    {
      // A string ("On"/"Off"/"PID"), NOT a boolean: sending true silently means Off.
      if (!v.is<const char *>())
        return err = "buzzer must be the string On or Off", false;
    }
    else if (k == "kitId")
    {
      if (!numInRange(v, 0, 1e9))
        return err = "kitId out of range", false;
    }
    else if (k == "parameters")
    {
      if (!v.is<JsonObjectConst>())
        return err = "parameters must be an object", false;
      for (JsonPairConst p : v.as<JsonObjectConst>())
      {
        String pk = p.key().c_str();
        if (pk == "detect shape")
        {
          if (!p.value().is<bool>())
            return err = "detect shape must be true/false", false;
        }
        else if (pk == "sg order" || pk == "sg window" || pk == "baseline start" ||
                 pk == "baseline range")
        {
          if (!numInRange(p.value(), 0, 255))
            return err = pk + " must be 0..255", false;
        }
        else if (!numInRange(p.value(), -1e6, 1e6))
          return err = pk + " must be a number", false;
      }
    }
    else if (k == "opto calibration")
    {
      if (!v.is<JsonObjectConst>())
        return err = "opto calibration must be an object", false;
      for (JsonPairConst p : v.as<JsonObjectConst>())
      {
        if (!arrayLenIs(p.value(), 10))
          return err = String(p.key().c_str()) + " must be exactly 10 values", false;
      }
    }
    else
      return err = "unknown setting: " + k, false;
  }
  return true;
}

// GET /config -> the whole parameter struct, in the shape POST /config accepts.
static void handleConfigGet(AsyncWebServerRequest *req)
{
  req->send(200, "application/json", paraToJson(_ForteSetting.parameter));
}

// recvData in ForteSetting is char[2048] and drainPending strlcpy()s into it; leave
// room for the "para version" key this handler injects before queueing.
static const size_t CFG_BODY_MAX = 1800;

// POST /config, body = a JSON subset of the same shape. Validated here, applied by
// SettingTask (which re-checks busy, closing the TOCTOU with a run starting).
//
// The body accumulates on the REQUEST, not in a static. AsyncWebServer interleaves
// body callbacks across concurrent requests, so a shared buffer gets reset to "" by
// whichever request starts next (index==0) and the other then parses a mix of both -
// two browser tabs saving at once could merge each other's keys into one EEPROM write.
// _tempObject is free()d by ~AsyncWebServerRequest, so it must be malloc'd POD: a
// new'd String there would have its heap buffer leaked (free() skips the destructor).
static void handleConfigPost(AsyncWebServerRequest *req, uint8_t *data, size_t len,
                             size_t index, size_t total)
{
  if (index == 0)
  {
    if (total == 0 || total > CFG_BODY_MAX)
    {
      req->send(413, "application/json", "{\"ok\":false,\"error\":\"settings too large\"}");
      return; // _tempObject stays NULL -> later chunks hit the guard below
    }
    req->_tempObject = malloc(total + 1);
    if (!req->_tempObject)
    {
      req->send(503, "application/json", "{\"ok\":false,\"error\":\"out of memory\"}");
      return;
    }
    ((char *)req->_tempObject)[0] = '\0';
  }
  if (!req->_tempObject)
    return; // rejected at index 0; we have already answered
  memcpy((char *)req->_tempObject + index, data, len);
  ((char *)req->_tempObject)[index + len] = '\0';
  if (index + len != total)
    return; // more chunks coming

  const char *body = (const char *)req->_tempObject;

  if (guardBusy(req))
    return;

  JsonDocument doc;
  if (deserializeJson(doc, body))
  {
    req->send(400, "application/json", "{\"ok\":false,\"error\":\"bad json\"}");
    return;
  }
  if (!doc.is<JsonObject>())
  {
    req->send(400, "application/json", "{\"ok\":false,\"error\":\"expected an object\"}");
    return;
  }

  String err;
  if (!validateConfig(doc.as<JsonObjectConst>(), err))
  {
    req->send(400, "application/json", String("{\"ok\":false,\"error\":\"") + err + "\"}");
    return;
  }

  // JsonDataConfig() applies NOTHING unless "para version" is present - and still
  // returns true, so a missing key looks like success. Inject the current one.
  doc["para version"] = _ForteSetting.parameter.para_version;

  String out;
  serializeJson(doc, out);
  if (out.length() >= 2048) // recvData is a fixed char[2048]
  {
    req->send(413, "application/json", "{\"ok\":false,\"error\":\"settings too large\"}");
    return;
  }
  if (!_ForteSetting.postConfigJson(out))
  {
    req->send(503, "application/json", "{\"ok\":false,\"error\":\"busy, retry\"}");
    return;
  }
  // "queued", NOT "saved": SettingTask applies it up to ~10 ms from now and may still
  // drop it if a run starts first. The client waits for this seq on the home event.
  req->send(200, "application/json",
            String("{\"ok\":true,\"queued\":true,\"seq\":") + _ForteSetting.cfgSeq + "}");
}

// ---- WiFi card -------------------------------------------------------------
// Scan must be ASYNC: a blocking scanNetworks() takes 2-4 s and would stall the single
// AsyncTCP task, freezing every SSE client and timing out this very request.
static void handleWifiScan(AsyncWebServerRequest *req)
{
  int16_t n = WiFi.scanComplete();
  if (n == WIFI_SCAN_FAILED)
  {
    // scanNetworks(true) enables STA alongside the SoftAP (WIFI_AP_STA) without
    // stopping it, so scanning from the fallback AP is safe.
    WiFi.scanNetworks(true);
    req->send(202, "application/json", "{\"scanning\":true}");
    return;
  }
  if (n == WIFI_SCAN_RUNNING)
  {
    req->send(202, "application/json", "{\"scanning\":true}");
    return;
  }

  JsonDocument doc;
  doc["scanning"] = false;
  JsonArray nets = doc["networks"].to<JsonArray>();
  for (int i = 0; i < n && i < 20; i++)
  {
    JsonObject o = nets.add<JsonObject>();
    o["ssid"] = WiFi.SSID(i);
    o["rssi"] = WiFi.RSSI(i);
    o["open"] = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
  }
  WiFi.scanDelete(); // next GET starts a fresh scan
  String out;
  serializeJson(doc, out);
  req->send(200, "application/json", out);
}

// POST /wifi?ssid=..&pass=.. -> save + reboot (the radio cannot switch under the
// browser: associating to a router on another channel drops every SoftAP client).
static void handleWifiSave(AsyncWebServerRequest *req)
{
  if (guardBusy(req))
    return;
  if (!req->hasParam("ssid", true))
  {
    req->send(400, "application/json", "{\"ok\":false,\"error\":\"missing ssid\"}");
    return;
  }
  String s = req->getParam("ssid", true)->value();
  String p = req->hasParam("pass", true) ? req->getParam("pass", true)->value() : "";

  // EEPROM string slots are fixed: ADDR_SSID=40..74, ADDR_PASSWORD=75..129. There is
  // no bounds check in saveSettingDevice(), so an over-long value silently overwrites
  // the NEXT field. Reject instead of truncating (a truncated password just fails to
  // connect and looks like a bad router).
  if (s.length() < 1 || s.length() > 32)
  {
    req->send(400, "application/json", "{\"ok\":false,\"error\":\"ssid must be 1..32 chars\"}");
    return;
  }
  if (p.length() > 54)
  {
    req->send(400, "application/json",
              "{\"ok\":false,\"error\":\"password max 54 chars (EEPROM slot limit)\"}");
    return;
  }
  if (!_ForteSetting.postWifiCreds(s, p))
  {
    req->send(503, "application/json", "{\"ok\":false,\"error\":\"busy, retry\"}");
    return;
  }
  // Answer BEFORE the reboot (SettingTask waits ~1.5 s) so the browser sees this.
  req->send(200, "application/json",
            String("{\"ok\":true,\"queued\":true,\"restarting\":true,\"seq\":") +
                _ForteSetting.cfgSeq + "}");
}

// POST /deviceid?id=.. -> global id_device + parameter.device_id (two stores).
static void handleDeviceId(AsyncWebServerRequest *req)
{
  if (guardBusy(req))
    return;
  if (!req->hasParam("id", true))
  {
    req->send(400, "application/json", "{\"ok\":false,\"error\":\"missing id\"}");
    return;
  }
  String id = req->getParam("id", true)->value();
  if (id.length() < 1 || id.length() > 9)
  {
    req->send(400, "application/json",
              "{\"ok\":false,\"error\":\"id must be 1..9 chars (char[10] in EEPROM)\"}");
    return;
  }
  if (!_ForteSetting.postDeviceId(id))
  {
    req->send(503, "application/json", "{\"ok\":false,\"error\":\"busy, retry\"}");
    return;
  }
  req->send(200, "application/json",
            String("{\"ok\":true,\"queued\":true,\"seq\":") + _ForteSetting.cfgSeq + "}");
}

// ---- Calib wizard ----------------------------------------------------------
// Calibration is a human-in-the-loop wizard on the device, not a routine we can just
// run: BLUE long-press -> RED starts a 55 C preheat -> both heaters >= 54 C then a
// 5 minute hold -> slot menu -> 4 measurements, each needing a tube physically swapped
// (300/200/100/0) and a BLUE press. The web drives the same state machine through the
// button queue; it never touches the sensor or the TFT itself.
static void handleCalib(AsyncWebServerRequest *req)
{
  String a = req->hasParam("action") ? req->getParam("action")->value() : "";
  e_statuslcd s = _displayCLD.type_infor;

  if (a == "start")
  {
    // Only from an idle screen, and it must be the long-press: a short BLUE press on
    // escreenStart runs epreheating80 (a normal run's preheat), not calibration.
    if (s != escreenStart)
    {
      req->send(409, "application/json",
                "{\"ok\":false,\"error\":\"calibration starts from the idle screen only\"}");
      return;
    }
    _buttonManager.postLongPress(B_GREEN);
  }
  else if (a == "next") // RED: preheat start / slot cycle / confirm
    _buttonManager.postShortPress(B_RED);
  else if (a == "measure") // BLUE: advance the wizard / take one calib point
    _buttonManager.postShortPress(B_GREEN);
  else if (a == "slot")
  {
    // The device picks the slot by cycling RED 0..9. Posting 7 presses would race the
    // 5 ms InputTask drain - pendingEvent is ONE slot, so presses inside the same tick
    // collapse and the slot lands wrong. `slot` is a plain int only eSelectSlot's
    // screen reads, so set it directly instead.
    if (s != eSelectSlot)
    {
      req->send(409, "application/json",
                "{\"ok\":false,\"error\":\"not on the slot screen\"}");
      return;
    }
    int n = req->hasParam("n") ? req->getParam("n")->value().toInt() : -1;
    if (n < 0 || n > 9)
    {
      req->send(400, "application/json", "{\"ok\":false,\"error\":\"slot must be 0..9\"}");
      return;
    }
    _displayCLD.slot = n;
    _displayCLD.changeScreen = true;
  }
  else if (a == "cancel")
  {
    // ONLY while actually in the calib flow. Without this guard, a cancel POST during
    // a run would yank type_infor to escreenStart and abort the amplification - the
    // cancel writes below are only safe/meaningful on a calib screen.
    if (calibStep(s)[0] == '\0')
    {
      req->send(409, "application/json",
                "{\"ok\":false,\"error\":\"not calibrating\"}");
      return;
    }
    // There is no clean abort on the device (WHITE on the slot screen calls
    // ESP.restart()). Unwind by hand: flag_calib_done latches on a pass and is only
    // cleared by BLUE on eSaveCalib - leaving it set makes the NEXT calibration's
    // buttons dead. type_calib left mid-sequence would expect the wrong tube.
    // ponytail: these are plain aligned scalar writes (atomic on Xtensa) and slopes are
    // only persisted on eSaveCalib, so a measurement in flight on SensorTask is
    // discarded, not corrupted. If a race ever bites, route cancel through a queue
    // drained by SensorTask like the settings path.
    _displayCLD.flag_calib_done = false;
    _sensor6035.type_calib = 0;
    _sensor6035.setStepeSensorwait();
    _displayCLD.type_infor = escreenStart;
    _displayCLD.changeScreen = true;
  }
  else
  {
    req->send(400, "application/json", "{\"ok\":false,\"error\":\"unknown action\"}");
    return;
  }
  req->send(200, "application/json", "{\"ok\":true}");
}

// GET /curve -> the WHOLE run, so a browser that connected late (or dropped) can
// redraw the full chart instead of only the points it happened to receive live, and
// so the Result tab can read back a finished run.
// {count, intervalMs, series:[[cal...] x10]} - calibrated, same formula as new_readings.
// Streamed (see CurveWriter): never allocates the whole payload.
static void handleCurve(AsyncWebServerRequest *req)
{
  // COUNTER is zeroed the moment a run finishes, but sensor67Value keeps the curve
  // (screen_Result even reloads it from EEPROM), so fall back to the retained length
  // to serve a FINISHED run.
  //
  // But NOT while amplifying: COUNTER is also 0 for the first ~20 s of a new run
  // (before round 1 completes), and sensor6035::clear() - the only thing that resets
  // lastRunLoops - is effectively never called per run (boot only). Without this guard
  // /curve would hand the PREVIOUS run's curve to a run that has just started, and the
  // live chart would open full of someone else's data.
  uint8_t n = _sensor6035.getCurrentLoop(); // live: completed rounds this run
  if (n == 0 && _displayCLD.type_infor != eoptoreading)
    n = _sensor6035.getLastRunLoops(); // reviewing a finished run
  if (n > 130)
    n = 130;

  auto w = std::make_shared<CurveWriter>();
  w->n = n;
  w->interval = (uint32_t)OPTO_INTERVAL; // ms per round -> x = index * interval
  req->send(req->beginChunkedResponse(
      "application/json",
      [w](uint8_t *buf, size_t maxLen, size_t) -> size_t
      { return w->fill(buf, maxLen); }));
}

// GET/POST /rename?slot=<0-9>&name=<str> -> persist to /slotnames.json.
static void handleRename(AsyncWebServerRequest *req)
{
  if (!req->hasParam("slot") || !req->hasParam("name"))
  {
    req->send(400, "application/json", "{\"ok\":false,\"error\":\"missing param\"}");
    return;
  }
  int slot = req->getParam("slot")->value().toInt();
  if (slot < 0 || slot >= 10)
  {
    req->send(400, "application/json", "{\"ok\":false,\"error\":\"bad slot\"}");
    return;
  }
  slotNames[slot] = req->getParam("name")->value();
  saveSlotNames();
  req->send(200, "application/json", "{\"ok\":true}");
}

// STA joined OR the SoftAP fallback is up -> a browser can reach us.
static bool networkUp() { return WiFi.status() == WL_CONNECTED || apActive; }

void dashboardStartAP()
{
  // Free the ~60KB of reserved Bluetooth memory: it's not needed in AP mode and,
  // left resident, starves the SoftAP's DHCP server + the dashboard -> clients
  // associate but never get an IP ("can't connect"). One-way; reboot restores BT/STA.
  releaseBluetoothStack();

  String ap = "RAPID-" + id_device;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap.c_str()); // open AP (no internet -> chart uses bundled highcharts.js)
  apActive = true;
  Serial.printf("[dash] SoftAP '%s' at http://%s/ | free=%u maxAlloc=%u\n",
                ap.c_str(), WiFi.softAPIP().toString().c_str(),
                ESP.getFreeHeap(), ESP.getMaxAllocHeap());
}

void dashboardBegin()
{
  if (started)
    return;

  // Hand back the ~60KB Bluetooth Classic stack before serving anything.
  //
  // Measured on STA with BT still resident: free=32KB, maxAlloc=16KB - logo.png
  // (2.3KB) served fine while index.html/style.css/script.js all TIMED OUT, because
  // AsyncTCP could not get buffers for a bigger response. That is the "web chap chon"
  // report. The SoftAP path already released BT (dashboardStartAP), which is exactly
  // why AP mode measured free=89KB/maxAlloc=55KB and worked.
  //
  // Trade-off (accepted): BT config via ForteSetting dies until reboot (GOTCHA 1, the
  // release is one-way). It was already dying on the first run anyway - screen_Result
  // releases BT before every upload - and the web Setting tab now covers what BT
  // config did.
  releaseBluetoothStack();

  if (!handlersReady) // register routes once; they persist across end()/begin()
  {
    if (!LittleFS.begin())
      Serial.println("[dash] LittleFS mount failed - UI files unavailable");
    loadSlotNames();
    dashServer.addHandler(&dashEvents);
    dashServer.on("/control", HTTP_ANY, controlHandler);
    dashServer.on("/home", HTTP_GET, [](AsyncWebServerRequest *req)
                  { req->send(200, "application/json", buildHomeJson()); });
    dashServer.on("/slots", HTTP_GET, handleSlots);
    dashServer.on("/rename", HTTP_ANY, handleRename);
    dashServer.on("/curve", HTTP_GET, handleCurve);
    // Setting tab
    dashServer.on("/config", HTTP_GET, handleConfigGet);
    dashServer.on("/config", HTTP_POST, [](AsyncWebServerRequest *r) {}, NULL, handleConfigPost);
    dashServer.on("/wifiscan", HTTP_GET, handleWifiScan);
    dashServer.on("/wifi", HTTP_POST, handleWifiSave);
    dashServer.on("/deviceid", HTTP_POST, handleDeviceId);
    dashServer.on("/calib", HTTP_POST, handleCalib);
    dashServer.on("/calib", HTTP_GET, handleCalib);

    // serveStatic LAST. Handlers are tried in registration order, so with it first
    // every API call first cost four failed LittleFS opens looking for /curve,
    // /curve.gz, /curve/index.html, /curve/index.html.gz (visible as the vfs_api
    // "does not exist" spam in the serial log) before falling through. Worse, a file
    // in data/ that happened to share an API name would shadow the route entirely.
    // "no-cache" = the browser MAY cache but MUST revalidate every load (a tiny
    // conditional GET -> 304 when unchanged, 200 with the new file after a uploadfs).
    // Without it, serveStatic sends an ETag but no Cache-Control, so browsers apply
    // heuristic freshness and keep serving the OLD script.js after an update until the
    // user does a hard refresh - which is exactly the "my change isn't showing" trap.
    dashServer.serveStatic("/", LittleFS, "/")
        .setDefaultFile("index.html")
        .setCacheControl("no-cache");
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
    // WiFi associates AFTER setup() returns, so start the server here the moment STA
    // (or the AP fallback) is up.
    if (!networkUp())
    {
      // Give STA a real chance before stranding the device on the SoftAP. setup()
      // used to wait 1.1 s and give up, but a router + DHCP routinely needs 1-3 s
      // (GOTCHA 5) - and dashboardStartAP() does WiFi.mode(WIFI_AP), killing STA with
      // no retry until reboot. So: wait STA_GRACE_MS from the first loop tick, and
      // only fall back if STA really has not come up.
      static uint32_t staSince = 0;
      if (staSince == 0)
        staSince = millis();
      if (!apActive && millis() - staSince > STA_GRACE_MS)
      {
        Serial.printf("[wifi] STA not up after %lu s -> SoftAP fallback\n",
                      (unsigned long)(STA_GRACE_MS / 1000));
        dashboardStartAP();
      }
      return;
    }
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
