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
#include "errorCheck.h"  // the per-slot error table behind GET /errors. Kept with the other
                         // project headers, i.e. BEFORE <ESPAsyncWebServer.h> - GOTCHA 3.
#include "updateOTA.h"   // otaState / checkFirmware(): the web Setting tab drives OTA
#include "wifiStore.h"   // saved networks (NVS): multi-network join fallback
#include <Preferences.h> // slot labels in NVS (see loadSlotLabels): survives `uploadfs`
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <DNSServer.h>     // captive portal on the SoftAP fallback (see dashboardStartAP)
#include <memory>          // shared_ptr: keeps the /curve stream state alive across chunk calls
#include "esp_heap_caps.h" // heap_caps_get_largest_free_block: internal-RAM diag for TLS -32512
#include <ESPmDNS.h>       // http://<id>.local/ : a stable name when DHCP moves the STA IP
#include "webAssets.h"     // GENERATED from data/ by tools/pio_gzip_data.py - the UI itself.
                           // Include from THIS translation unit only (static arrays).

// The device ID is protoID (= _ForteSetting.parameter.device_id, ForteSetting.h). There is no
// id_device global any more - it was a second store of the same value and the two drifted.
extern String ssid;               // defined in Bluetooth.cpp - the preferred network
void releaseBluetoothStack(void); // defined in Bluetooth.cpp - frees ~60KB BT memory

// How many people may watch the dashboard live at once. Enforced twice because neither
// mechanism covers both modes: as softAP's max_connection (SoftAP, refused at association)
// and as an SSE-handler filter (works on STA as well, where the whole LAN can reach us).
// Note this counts CONNECTIONS, not people - a phone with two tabs open uses two. It is an
// access policy, not a heap limit: measured cost is ~660 B per client and the largest
// contiguous block doesn't move at all, so raising it is safe.
static constexpr size_t MAX_VIEWERS = 2;

static AsyncWebServer dashServer(80);
static AsyncEventSource dashEvents("/events");
static DNSServer dnsServer; // only running while apActive (captive portal)

// Enforces MAX_VIEWERS on STA, where softAP's max_connection has no say (the whole LAN can
// reach us). Registered BEFORE dashEvents: it claims /events ONLY while we're already full,
// otherwise canHandle() says no and the request falls through to the real SSE handler.
//
// Why a hand-rolled handler instead of dashServer.on("/events", ...).setFilter(...):
// AsyncCallbackWebHandler::canHandle() starts with `!request->isHTTP() -> return false`, and
// an SSE request is RCT_EVENT, not HTTP - so that handler never sees /events at all (measured:
// the 3rd client still got 200). AsyncEventSource::canHandle is `final`, so subclassing the
// event source is out too.
//
// And deliberately NOT the library's own idiom (onConnect + client->close()): _addClient()
// invokes the connect callback while holding _client_queue_lock, and close() re-enters that
// same non-recursive mutex through _handleDisconnect -> the exact self-deadlock of GOTCHA 14.
// count() takes that lock as well, so it may only be called from here - never from a callback.
//
// A refused browser still gets the page (static files + /home) and its EventSource retries on
// its own, so it slots in as soon as somebody leaves. That retry is also what makes a reload
// safe: the old connection is gone by the time the new one asks again.
class ViewerCapHandler : public AsyncWebHandler
{
public:
  bool canHandle(AsyncWebServerRequest *request) const override
  {
    return request->isSSE() && request->url() == "/events" &&
           dashEvents.count() >= MAX_VIEWERS;
  }
  void handleRequest(AsyncWebServerRequest *request) override
  {
    request->send(503, "text/plain", "Too many viewers");
  }
};
static ViewerCapHandler viewerCap;
static bool started = false;
static bool apActive = false;      // true when running as SoftAP fallback (no STA)
// Set by dashboardRequestAP() (InputTask), consumed by dashboardLoop() (NetworkTask). volatile
// because those are two tasks and the write is a single byte - same shape as otaState.
static volatile bool apRequested = false;
// True only when the AP came up because someone ASKED for it, not from the boot fallback. The
// exit path reboots on the first and must not on the second.
static bool apOnDemand = false;
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
// collapses everything unknown to phase "idle", so the calib menus, the OTA prompt and
// the tube waits all LOOK idle to the client - locking on `phase` would let a POST land
// mid-calibration (which drives heaters and writes EEPROM from DisplayTask) or mid-run.
//
// eUpdateOTA is NOT busy, and that is load-bearing, not cosmetic: updateFirmware() now
// re-checks this before downloading, and dashboardLoop() gates the deferred reboot on it.
// The prompt is only ever reached from the boot-time checkFirmware() (the web check passes
// promptOnDevice=false), so nothing is running behind it - but counting it as busy made
// pressing RED on the prompt abort its own download, and left a downloaded image parked
// forever because the screen it sits on never becomes "idle".
static bool isBusy(e_statuslcd s)
{
  switch (s)
  {
  case escreenStart:    // idle start screen
  case escreenFinished: // run done, results on screen
  case escreenReview:   // reviewing results
  case eSettingMenu:    // in the on-device setting menu
  case eShowQR:         // just showing the dashboard QR; nothing is running
  case errprocess:      // error screen; nothing is running
  case eUpdateOTA:      // "update available" prompt; nothing is running either
    return false;
  default:
    return true; // running, heating, calibrating, uploading, OTA, tube waits...
  }
}

// An OTA download is busy no matter what the screen says. type_infor does NOT change while
// httpUpdate.update() streams for ~2 minutes (waittingUpdate() only paints), so screen state
// alone reports "idle" for the whole window - on the web-initiated path that was already true
// before eUpdateOTA joined the allowlist. Anything that reboots or writes flash in that window
// is destructive: POST /wifi reboots mid-download, and POST /otaupload would drive the SAME
// global Update singleton from AsyncTCP while NetworkTask is writing the partition.
bool dashboardDeviceBusy()
{
  return otaState == OTA_UPDATING || isBusy(_displayCLD.type_infor);
}

bool dashboardIsAP() { return apActive; }
void dashboardRequestAP() { apRequested = true; }
bool dashboardApStartedOnDemand() { return apOnDemand; }

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

// Results for the last run are computed and cached. Defined here rather than beside
// dashboardSetResults() because fillStatus needs it: escreenFinished covers BOTH the ~90 s in
// which screen_Result() is still computing and uploading AND the finished screen afterwards,
// and this flag is the only thing that tells them apart.
static bool gResultsReady = false;

// Derive the home-screen status block from the LCD state machine.
//
// EVERY state an operator can sit in gets a case. It used to cover 12 of 38 and let the rest
// fall through to "Idle / Waiting for a run to start.", which is not a small cosmetic gap: at
// ewaitphase2 the machine is waiting for someone to take the hot lysis tube out, and the web
// said "Idle" while the green chip beside it said "Amplification" - the same screen telling the
// operator there is nothing to do and offering them a labelled button.
//
// Two rules for the `phase` string, both learned the hard way:
//   - NEVER report "idle" for a state where RED means something other than "start naming":
//     data/script.js:67 rewrites a red press into the naming gate whenever phase == "idle".
//     That is why ewaitLysisTube has its own phase, and why the ones added here do too.
//   - "finished" is not a free label either - the client's chartMode includes it, so handing it
//     out puts the chart on Home. States that merely follow a run get their own phase instead.
// Unknown phases are safe: the client treats anything it does not name as the plain view.
//
// bt / tp are the temperatures buildHomeJson already fetched (bottom = {lysis, ampLeft,
// ampRight}, hotlid = {topLeft, topRight, ambient}); passed in so this does not read them twice.
static void fillStatus(JsonObject status, e_statuslcd s, const double *bt, const double *tp)
{
  const char *phase = "idle";
  String title = "Idle";
  String sub = "Waiting for a run to start.";
  const parastructure &p = _ForteSetting.parameter;

  switch (s)
  {
  // ---- lysis leg ---------------------------------------------------------------------
  case epreheating80:
    phase = "heater";
    title = "Heating lysis block";
    // The target, and how far off it is. "Warming to 80 C" never moved, so it read like a
    // stuck screen on the ~10 minutes this takes.
    sub = String(r1(bt[0]), 1) + " / " + String(p.lysisTemp, 1) + " C";
    break;
  // No case for eheathotlid1: nothing in src/ ever assigns it, and displayLCD's own switch has
  // no case either, so it would describe a screen that does not exist.
  case eheatLysis:
    phase = "heater";
    title = "Lysis running";
    // Ceil to whole minutes like a person reads a clock; the TFT shows the same countdown.
    sub = "~" + String((_displayCLD.lysisRemainSec() + 59) / 60) + " min left, block " +
          String(r1(bt[0]), 1) + " C";
    break;
  case ewaitphase2:
    // The machine is waiting on a PERSON, with a hot tube in the block. Its own phase: RED does
    // not mean "start naming" here, and reporting "idle" made the web rewrite it into that.
    phase = "waitphase2";
    title = "Remove lysis tube";
    sub = "Take the tube out and close the lid, then press Amplification (green).";
    break;
  // ---- amplification leg -------------------------------------------------------------
  case eheating67:
    phase = "heater";
    title = "Heating to " + String(p.amplifTemp, 1) + " C";
    sub = "Blocks " + String(r1(bt[1]), 1) + "/" + String(r1(bt[2]), 1) + " C, lids " +
          String(r1(tp[0]), 1) + "/" + String(r1(tp[1]), 1) + " C";
    break;
  case epreheat67:
  {
    // Split from eheating67: there the heaters are still climbing, here they are AT temperature
    // and the wait is the hold plus the optics. Two separate gates, so do not promise a single
    // countdown - when the hold has run out and the screen has not moved on, the honest answer
    // is that something else is still not ready.
    phase = "heater";
    title = "Warming up optics";
    uint32_t now = millis();
    uint32_t deadline = _PIDControl.timeStartWait + _PIDControl.hotlidWaitMs;
    // timeStartWait == 0 is a real value, not "unset": the post-lysis green press writes it to
    // make the hold count as already served, so there is no countdown to show on that path.
    if (_PIDControl.timeStartWait && deadline > now)
      sub = "~" + String(((deadline - now) / 1000 + 59) / 60) + " min of temperature hold left";
    else
      sub = "Waiting for the lids and the optics to be ready";
    break;
  }
  case eoptoreading:
  {
    phase = "amplification";
    title = "Amplification";
    // The clock and the acquisition are independent counters: the run ends when the round count
    // reaches the target, so the timer can reach zero with rounds still to go. Show both, and
    // stop claiming minutes once the clock has run out.
    uint32_t left = _displayCLD.ampRemainSec();
    if (left)
      sub = "~" + String((left + 59) / 60) + " min left";
    else
      sub = "Finishing the last rounds";
    sub += " - round " + String(_sensor6035.getCurrentLoop()) + "/" + String(p.amplification_time);
    break;
  }
  // ---- end of run --------------------------------------------------------------------
  case escreenFinished:
    // ONE enum, two very different screens: screen_Result() spends ~30-90 s computing the
    // outcome and blocking in mbedTLS uploading it, and only then is anything actually ready.
    // Saying "Results ready." for that whole window invited a reload that found an empty table.
    phase = "finished";
    if (gResultsReady)
    {
      title = "Run complete";
      sub = "Results ready.";
    }
    else
    {
      title = "Finishing the run";
      sub = "Computing results and uploading - this can take a minute.";
    }
    break;
  case escreenResult:
    phase = "finished";
    title = "Results";
    sub = "Reading the run back from the device.";
    break;
  case escreenErrorResult:
    // Its OWN phase, and deliberately not the same "error" as errprocess: this is the operator
    // ASKING to see the error table (RED on the finished screen), not the machine failing. Home
    // swaps the chart for that table on this phase, and doing that on a real fault would be
    // wrong. Also not "idle" - red here does not open the naming gate.
    phase = "errortable";
    title = "Sensor error table";
    sub = "Per-channel errors for the last run. White returns.";
    break;
  case escreenReview:
    phase = "review";
    title = "Reviewing last run";
    sub = "Reading the stored run out of memory.";
    break;
  case errprocess:
    phase = "error";
    title = "Error";
    sub = "Check the device.";
    break;
  case escreenRestart:
  case ebuttonrestart:
  case ewaitingtimeout:
    // These three hold whichever restart prompt is already on the TFT; from the web they are one
    // thing - the machine is on its way back to the start screen.
    phase = "restart";
    title = "Restarting";
    sub = "Returning to the start screen.";
    break;
  // ---- on-device menus / tools -------------------------------------------------------
  case eSettingMenu:
    phase = "setting";
    title = "Settings menu (on the device)";
    sub = "Green: WiFi / web QR - Red: upload the last run - White: Bluetooth.";
    break;
  case eUpLoadData:
    phase = "upload";
    title = "Uploading last run";
    // No countdown on purpose: nothing timestamps the start of the upload, so any number here
    // would be invented.
    sub = "Re-sending the stored run to the cloud - this can take a minute.";
    break;
  case eShowQR:
    phase = "qr";
    title = "Dashboard QR on the screen";
    // Reads the radio, same rule as screen_QR() and net.ssid: the builder feeds softAP(),
    // whoever REPORTS the name asks the driver.
    if (dashboardIsAP())
      sub = "Join " + WiFi.softAPSSID() + " then open 192.168.4.1";
    else
      sub = "Scan it, or open http://" + WiFi.localIP().toString() + "/";
    break;
  case eUpdateOTA:
    // One enum, two screens: the offer, and the download. dashboardDeviceBusy() already
    // distinguishes them the same way.
    if (otaState == OTA_UPDATING)
    {
      phase = "ota";
      title = "Installing firmware";
      sub = "Downloading the update - do not power off. The device reboots when it finishes.";
    }
    else
    {
      phase = "ota";
      title = "Firmware update available";
      sub = "The device is asking whether to install it. Green accepts, red declines.";
    }
    break;
  // ---- calibration wizard (on-device; the web card is hidden but the machine still runs it) --
  case ecalibPreheatStart:
    phase = "calib";
    title = "Calibration - ready to preheat";
    sub = "Heaters are off. Red starts warming the amplification block to 55 C.";
    break;
  case ecalibPreheating:
    phase = "heater";
    title = "Calibration heating";
    sub = "Warming to 55 C - now " + String(r1(bt[1]), 1) + "/" + String(r1(bt[2]), 1) + " C";
    break;
  case ecalibSelect:
    phase = "calib";
    title = "Preheated to 55 C";
    sub = "Blue = calibrate, Red = run amplification. Block at " + String(r1(bt[1]), 1) + "/" +
          String(r1(bt[2]), 1) + " C.";
    break;
  case eSelectMode:
    phase = "calib";
    title = "Calibration - choose action";
    sub = "Blue = calibrate a slot, Red = set LED power.";
    break;
  case eSelectAmpli:
    phase = "calib";
    title = "Calibration - amplification selected";
    sub = "Preparing the amplification block.";
    break;
  case eSelectSlot:
    phase = "calib";
    title = "Calibration - pick a slot";
    sub = "Slot " + String(_displayCLD.slot + 1) + " selected. Red steps, Blue confirms.";
    break;
  case eCalibrating:
    phase = "calib";
    title = "Calibration - insert tube";
    sub = "Slot " + String(_displayCLD.slot + 1) + ", point " +
          String(_sensor6035.type_calib + 1) + " of 4. Swap the tube, then press Blue.";
    break;
  case eWaitingCalib:
    phase = "calib";
    title = "Calibration - measuring";
    sub = "Reading slot " + String(_displayCLD.slot + 1) + ". Do not open the lid.";
    break;
  case eCalibComplete:
    phase = "calib";
    title = "Calibration finished";
    sub = "Slot " + String(_displayCLD.slot + 1) + " - check the slope on the device screen.";
    break;
  case eSaveCalib:
    phase = "calib";
    title = "Calibration saved";
    sub = "Slot " + String(_displayCLD.slot + 1) + " slope " +
          String(p.slopes[_displayCLD.slot], 3) + " written to memory.";
    break;
  case eSetPowerLed:
    phase = "calib";
    title = "Set LED power";
    sub = "Slot " + String(_displayCLD.slot + 1) + " - editing on the device.";
    break;
  case eSavePowerLed:
    phase = "calib";
    title = "LED power saved";
    sub = "Slot " + String(_displayCLD.slot + 1) + " = " +
          String(p.led_power[_displayCLD.slot]) + ".";
    break;
  case ewaitLysisTube:
    // NOT "idle". It shares a screen family with escreenStart but RED means something else
    // here ("Start lysis" vs "Amplification"), and the web client keys off phase: at idle it
    // rewrites a red press into the naming gate (script.js: btn = "ampname"). Sharing "idle"
    // therefore made the web red chip light up and send a request that had nothing to do with
    // lysis - the machine never left this screen, which is the "red lights up but lysis never
    // starts" report. One phase per meaning; anything else that reads phase inherits the fix.
    phase = "waitlysis";
    title = "Insert lysis tube";
    // "Waiting for user" said nothing the title had not; the block temperature is the thing an
    // operator standing at the machine actually wants before dropping a tube in.
    sub = "Block at " + String(r1(bt[0]), 1) + " C - press Start lysis";
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
  // escreenStart is the genuine idle screen, and the transient states between screens have
  // nothing worth reporting - they are gone before a 1 Hz frame can show them.
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
    white = "QR / Web"; // WHITE opens the dashboard QR screen
    break;              // full flow / amp-only
  case eShowQR:
    white = "Return";
    break;
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
  case escreenErrorResult:
    // The screen the RED above leads to. It had no case, so all three chips came up blank on
    // the one screen whose printed instruction is "Press white key to test next" - the button
    // worked, it just was not labelled. WHITE falls through handleShortPress_White's checks to
    // ebuttonrestart, so "Next test" is what it actually does.
    white = "Next test";
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
  doc["device"] = protoID;
  doc["company"] = "Fortebiotech";

  // Which network the machine is ACTUALLY on, pushed live on the 1 s home event. Without
  // this the web had no field that changed when the device switched WiFi (or fell back to
  // SoftAP), so it looked "out of sync" with the machine. If the switch moved the device
  // to a different subnet the browser can't reach it anyway - but on the same subnet this
  // now updates within a second.
  JsonObject net = doc["net"].to<JsonObject>();
  bool ap = dashboardIsAP();
  net["ap"] = ap;
  // Ask the RADIO what it is broadcasting; do not rebuild the name from the device ID.
  // dashboardApName() is what the SSID SHOULD be, softAPSSID() is what is on the air, and those
  // two answers differ from the moment the ID changes until the deferred reboot re-announces it.
  // Reporting the first one told the operator to look for a network that was not there.
  net["ssid"] = ap ? WiFi.softAPSSID()
                   : (WiFi.status() == WL_CONNECTED ? WiFi.SSID() : String(""));
  net["ip"] = (ap ? WiFi.softAPIP() : WiFi.localIP()).toString();
  // TEMPORARY (2026-07-27): the L3 config DHCP actually handed us. Serving HTTP on the LAN
  // proves nothing about reaching the internet - same-subnet traffic needs neither a gateway
  // nor DNS. A missing/wrong gw explains "EHOSTUNREACH in 9 ms", and a missing/off-subnet
  // dns explains "hostByName(): DNS Failed", which is exactly the upload+OTA failure shape.
  if (!ap)
  {
    net["gw"] = WiFi.gatewayIP().toString();
    net["mask"] = WiFi.subnetMask().toString();
    net["dns1"] = WiFi.dnsIP(0).toString();
    net["dns2"] = WiFi.dnsIP(1).toString();
    net["rssi"] = WiFi.RSSI();
  }

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

  fillStatus(doc["status"].to<JsonObject>(), _displayCLD.type_infor, bt, tp);

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

// ---- Process tab: slot labels (persisted to NVS) + cached results ----
//
// These used to be /slotnames.json + /slotsamples.json on LittleFS. They live in NVS now,
// for the same reason the saved WiFi list does (wifiStore.cpp): `uploadfs`/`uploadall`
// reflash the WHOLE spiffs partition from data/, so anything written there at runtime is
// WIPED by every UI update - the operator's labels included. NVS is a separate partition
// (0x9000) that uploadfs never touches. With this moved, nothing in the firmware needs
// LittleFS at all, so an empty/corrupt/never-formatted spiffs has no consequence.
//
// One JSON string per set (2 NVS entries, not 20): the whole set is rewritten on every
// change anyway, and 10 short strings cost less as one blob than as ten keys.
// NVS is thread-safe internally and has no shared RAM buffer to double-free the way the
// EEPROM library does (CLAUDE.md Setting #2), so /rename may write straight from AsyncTCP.
// NOT static: postData_GoogleSheet() reads these for the "nameSlot" array in the upload
// payload (declared in webDashboard.h). slotSamples stays file-local on purpose - the sample
// label is web-only by design, it does not go to the cloud or the TFT.
//
// ponytail: read cross-task without a lock. /rename writes slotNames[slot] from AsyncTCP while
// the upload reads it from DisplayTask, and String assignment is not atomic - a rename landing
// in that exact window can hand the reader a freed buffer. Left as-is because the window is one
// run-end upload vs a manual label edit, and the payload field is a label, not a measurement.
// If it ever needs to be tight: snapshot all 10 into a local array behind gI2CMutex-style
// guard at the top of postData_GoogleSheet, or move the labels behind an accessor that copies.
String slotNames[10];          // disease per slot (fixed shrimp-disease list)
static String slotSamples[10]; // free-text sample label per slot
static float gCT[10] = {0};
static char gResult[10] = {0};
// Shape measurements, published from bResultGet() alongside the CT/outcome cache above.
// -1 = not measurable on that curve, which the route reports as null rather than as a number.
static double gWindowRate[10] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
static double gRiseWidth[10] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
static uint8_t gShapeFlag[10] = {0};

// Per-slot sensor-error snapshot for GET /errors - the same table the machine draws on
// screen_errorResult() (RED on the finished/review screens).
//
// A SNAPSHOT, not a live read, and that is the whole design. `error.error` is a
// std::vector that ControlTask push_back()s from ~28 call sites at any moment; a
// push_back reallocates, so iterating it from the AsyncTCP task would be a
// use-after-free waiting for a bad run. Taken in dashboardSetResults() instead, which
// runs on DisplayTask (screen_Result) or SettingTask (/reviewlast) - the same task that
// already reads the vector to draw the TFT table, so this adds no exposure that was not
// there. It also lands at the same instant as the CT/outcome cache, so /errors and
// /slots can never describe different runs (the 2026-07-21 table-vs-chart desync).
static ErrorRecord_t gErrRec[10];
static bool gErrHas[10] = {false};
// (definition hoisted above fillStatus - see there)

static const char *SLOT_NS = "slotlabels";

static void loadSlotLabels(const char *key, String *dst)
{
  Preferences p;
  if (!p.begin(SLOT_NS, /*readOnly=*/true))
    return; // namespace never created -> labels stay empty
  String json = p.getString(key, "");
  p.end();
  if (!json.length())
    return;
  JsonDocument doc;
  if (!deserializeJson(doc, json))
    for (int i = 0; i < 10; i++)
      dst[i] = doc[i] | "";
}

static void saveSlotLabels(const char *key, const String *src)
{
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < 10; i++)
    arr.add(src[i]);
  String json;
  serializeJson(doc, json);
  Preferences p;
  if (!p.begin(SLOT_NS, /*readOnly=*/false))
  {
    Serial.println("[dash] NVS open failed for slot labels");
    return;
  }
  p.putString(key, json);
  p.end();
}

// Drop every slot label, RAM and NVS. Called when a new run cycle starts (dashboardLoop).
//
// The labels belong to ONE run. Nothing used to clear them, so a run the operator never
// named was uploaded under the PREVIOUS run's disease names - postData_GoogleSheet reads
// slotNames[] straight into "nameSlot", so the result was filed against the wrong assay,
// silently. Blank is the designed fallback ("N/A" in the payload, "#N" in the chart legend).
//
// NVS too, not just RAM: dashboardBegin() reloads from NVS, so clearing only RAM would put
// the stale names right back after a reboot.
static void dashboardClearSlotLabels()
{
  bool any = false;
  for (int i = 0; i < 10; i++)
    if (slotNames[i].length() || slotSamples[i].length())
      any = true;
  if (!any)
    return; // already blank - don't spend an NVS write on every run cycle
  for (int i = 0; i < 10; i++)
  {
    slotNames[i] = "";
    slotSamples[i] = "";
  }
  saveSlotLabels("names", slotNames);
  saveSlotLabels("samples", slotSamples);
  Serial.println("[dash] new run cycle - slot labels cleared");
}

// Called from screen_Result() when a run's results are computed.
void dashboardSetResults(const float *ct, const char *result)
{
  for (int i = 0; i < 10; i++)
  {
    gCT[i] = ct[i];
    gResult[i] = result[i];
    // Same query the TFT's error table runs (displayLCD.cpp screen_errorResult): light-sensor
    // module, "no data", first reading step, this slot. Mirroring the query rather than
    // inventing a broader one keeps the two screens from ever disagreeing about the same run.
    uint8_t k = error.searchError(errorLightSensor, errorNoData, eSensor1stReading, i);
    gErrHas[i] = (k != 255);
    if (gErrHas[i])
      gErrRec[i] = error.error[k]; // copy the record out; the route must not touch the vector
  }
  gResultsReady = true;
}

void dashboardSetShape(const double *window_rate, const double *rise_width, const uint8_t *flag)
{
  for (int i = 0; i < 10; i++)
  {
    gWindowRate[i] = window_rate[i];
    gRiseWidth[i] = rise_width[i];
    gShapeFlag[i] = flag[i];
  }
}

// Invalidate the cached results (see header). Pairs with _sensor6035.clear() at run
// start so the table and the chart share one lifecycle: both go empty together, then
// both come back together from EEPROM on the next Result view (POST /reviewlast).
void dashboardClearResults()
{
  gResultsReady = false;
}

// GET /slots -> {ready, slots:[{name, sample, ct, result} x10]}. ct only for P/S (has CT).
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
    s["sample"] = slotSamples[i];
    // 'F' (v2.4.3AT, shape-flagged) carries a CT like P and S do. Withholding it would leave the
    // operator disputing the call with nothing to dispute it WITH - the time the well rose is the
    // most useful single number on that row.
    if (ready && (gResult[i] == 'P' || gResult[i] == 'S' || gResult[i] == 'F'))
      s["ct"] = r1(gCT[i]);
    else
      s["ct"] = nullptr;
    s["result"] = ready ? String(gResult[i]) : String("");
    // Shape rule. Reported on every well, not only flagged ones: the point of emitting these is
    // to accumulate the labelled data that would let the thresholds be set from measurement.
    // null, not -1, when the curve gave nothing measurable - the client must not plot a "-1".
    if (ready && gWindowRate[i] >= 0)
      s["rate"] = r1(gWindowRate[i]);
    else
      s["rate"] = nullptr;
    if (ready && gRiseWidth[i] >= 0)
      s["rise"] = r1(gRiseWidth[i]);
    else
      s["rise"] = nullptr;
    s["shape"] = ready ? gShapeFlag[i] : 0;
  }
  // Which build this is, so the Result tab can say whether a flag overturned the call or merely
  // annotates it. The client must not infer this from the outcome letters: in the v2.4.3a build a
  // flagged well reads "N" with no trace of the flag in that letter alone.
#ifdef SHAPE_RULE_NEGATIVE
  doc["shapeMode"] = "negative";
#else
  doc["shapeMode"] = "flag";
#endif
  String out;
  serializeJson(doc, out);
  req->send(200, "application/json", out);
}

// GET /errors -> {ready, slots:[{code, text} x10]} - the web copy of the machine's own error
// table (RED on the finished/review screens, screen_errorResult()).
//
// Gated on the SAME `ready` expression as /slots: this table belongs to a run, and during a new
// amplification the snapshot still describes the previous one.
//
// Reads only the gErrRec snapshot, never error.error - see the note beside gErrRec. decodeError()
// takes the record BY VALUE and only indexes static string tables, so calling it here is safe.
// Ten entries, so no chunked writer is needed (GOTCHA 10 is about the full-run payloads).
static void handleErrors(AsyncWebServerRequest *req)
{
  bool ready = gResultsReady && (_displayCLD.type_infor != eoptoreading);

  JsonDocument doc;
  doc["ready"] = ready;
  JsonArray arr = doc["slots"].to<JsonArray>();
  for (int i = 0; i < 10; i++)
  {
    JsonObject s = arr.add<JsonObject>();
    if (ready && gErrHas[i])
    {
      // Same 4-digit encoding the TFT prints, so an operator can read one screen to the other.
      s["code"] = error.EncodeError(gErrRec[i]);
      s["text"] = error.decodeError(gErrRec[i]);
    }
    else
    {
      s["code"] = nullptr;
    }
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

    // "PCB version" reaches strlcpy(parameter.PCB_version, ...) in JsonDataConfig(),
    // a char[10] at offset 14 of parastructure: unbounded it walks over slopes/origins/
    // kpid and past the struct before EEPROM.commit(). "para version" is injected below
    // (overwriting whatever came in), but it lands in the same kind of slot - check both.
    if (k == "para version" || k == "PCB version" || k == "device ID" || k == "units")
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
        else if (pk == "sg window")
        {
          // sg_smooth() returns an all-zero vector when the analysed window is shorter than
          // 2*sg_window+2, and its error report is commented out (Alg/sgsmooth.cpp:537-541), so a
          // large value here silently turns every verdict into an accident. A window trimmed to a
          // late break can be as short as 10 samples, which is already the limit at the default 4.
          if (!numInRange(p.value(), 1, 10))
            return err = pk + " must be 1..10", false;
        }
        else if (pk == "sg order" || pk == "baseline start" || pk == "baseline range")
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

// POST /reviewlast -> reload the last completed run from EEPROM and recompute its
// results, so the Result tab can review it after a reboot (the RAM cache is gone).
// Only when idle: the reload overwrites sensor67Value, which SensorTask owns mid-run.
// SettingTask does the work (bResultGet is a heavy JSON+algo pass); the client polls
// /slots until ready.
static void handleReviewLast(AsyncWebServerRequest *req)
{
  // Require ?go=1. The route is registered with WebServer.h's HTTP_POST (== 3) while
  // AsyncWebServer matches bitwise against its own flags (GET == 1), so `3 & 1` lets a bare
  // GET land here (GOTCHA 3) - and this handler, unlike /wifi or /deviceid, has no parameter
  // to reject on. A browser link prefetch or a scanner would then queue the review: an ~8 s
  // EEPROM read on SettingTask that overwrites the live sensor67Value buffer. Checking a
  // query param keeps req->method() out of the code (see test_no_method_branch.py).
  // Safe to tighten: the UI is embedded in this same firmware, so client and route ship
  // together - there is no older page left anywhere that could still call the old form.
  if (!req->hasParam("go"))
  {
    req->send(400, "application/json", "{\"ok\":false,\"error\":\"use POST /reviewlast?go=1\"}");
    return;
  }
  if (dashboardDeviceBusy())
  {
    req->send(409, "application/json", "{\"ok\":false,\"error\":\"device busy\"}");
    return;
  }
  if (!_ForteSetting.postReviewLast())
  {
    req->send(503, "application/json", "{\"ok\":false,\"error\":\"busy, retry\"}");
    return;
  }
  req->send(200, "application/json", "{\"ok\":true,\"queued\":true}");
}

// GET /ota -> what is installed, what (if anything) is offered, and where the OTA state
// machine stands. Pure reads of volatile/String state - no network, no EEPROM - so this
// is safe on AsyncTCP.
static void handleOtaStatus(AsyncWebServerRequest *req)
{
  static const char *NAMES[] = {"idle", "available", "accepted",
                                "updating", "failed", "dismissed"};
  OtaState st = otaState;

  JsonDocument doc;
  doc["version"] = FirmwareVer; // human-readable build, e.g. "v2.4.4"
  doc["state"] = NAMES[st <= OTA_DISMISSED ? st : 0];
  doc["hasUpdate"] = (st == OTA_AVAILABLE);
  doc["busy"] = (st == OTA_UPDATING);
  doc["checked"] = otaLastCheck != 0;  // false = not checked since boot
  doc["checkFailed"] = otaCheckFailed; // checked, but the server GET errored
  if (fwVer.length())
    doc["newVersion"] = fwVer; // the .bin file name the server is offering
  // Without WiFi neither the check nor the download can work; let the UI say so
  // instead of offering a button that silently does nothing.
  doc["online"] = (WiFi.status() == WL_CONNECTED);

  String out;
  serializeJson(doc, out);
  req->send(200, "application/json", out);
}

// POST /ota?action=check|update
//   check  -> queue checkFirmware() onto SettingTask (blocking HTTPS; never on AsyncTCP)
//   update -> hand the OTA state machine to NetworkTask, which already polls for
//             OTA_USER_ACCEPTED in updateFirmware(). One volatile byte, so writing it
//             from here is safe; the download itself never touches this task.
// Both are refused while the machine is busy: an OTA ends in ESP.restart(), and doing
// that mid-run would destroy the sample AND the record of it.
// Single /ota entry point (HTTP_ANY). Dispatch on the `action` query param instead of the
// method: registering GET+POST separately makes the GET handler grab the POST (GOTCHA 3,
// 1 & 3 != 0), so `POST /ota?action=check` would silently return status and never queue.
static void handleOtaAction(AsyncWebServerRequest *req); // fwd decl
static void handleOta(AsyncWebServerRequest *req)
{
  if (req->hasParam("action")) // ?action=check|update -> act; otherwise report status
    handleOtaAction(req);
  else
    handleOtaStatus(req);
}

static void handleOtaAction(AsyncWebServerRequest *req)
{
  String action = req->hasParam("action") ? req->getParam("action")->value() : "";

  if (dashboardDeviceBusy())
  {
    req->send(409, "application/json", "{\"ok\":false,\"error\":\"device busy\"}");
    return;
  }
  if (WiFi.status() != WL_CONNECTED)
  {
    req->send(409, "application/json", "{\"ok\":false,\"error\":\"no internet\"}");
    return;
  }

  if (action == "check")
  {
    if (!_ForteSetting.postOtaCheck())
    {
      req->send(503, "application/json", "{\"ok\":false,\"error\":\"busy, retry\"}");
      return;
    }
    req->send(200, "application/json", "{\"ok\":true,\"queued\":true}");
    return;
  }

  if (action == "update")
  {
    // Only from AVAILABLE: starting a download without a checked, newer build would
    // re-flash the same version (or an empty fwUrl).
    if (otaState != OTA_AVAILABLE)
    {
      req->send(409, "application/json",
                "{\"ok\":false,\"error\":\"no update available\"}");
      return;
    }
    otaState = OTA_USER_ACCEPTED; // NetworkTask::updateFirmware() picks this up
    req->send(200, "application/json", "{\"ok\":true,\"started\":true}");
    return;
  }

  req->send(400, "application/json", "{\"ok\":false,\"error\":\"unknown action\"}");
}

// ---- OTA from a local .bin -------------------------------------------------------
// Second way in, for a machine with no internet (SoftAP) or a build that is not on
// on the server yet: the browser POSTs the firmware image and we stream it straight into the
// OTA partition. AsyncWebServer hands us the body in ~1-4 KB chunks, which is what makes
// this safe to do from the web task - we never hold the whole 2.3 MB anywhere.
static bool otaUpFail = false; // refused/aborted: swallow the remaining chunks
// Why the refusal, in the reply and not only on a serial cable nobody has plugged in. Same
// lifecycle as otaUpFail (both reset at index 0), so it cannot leak into another request.
static const char *otaUpErr = "upload failed";
static uint32_t otaRestartAt = 0; // reboot after the reply has been flushed

static void handleOtaUpload(AsyncWebServerRequest *req, const String &filename,
                            size_t index, uint8_t *data, size_t len, bool final)
{
  if (index == 0) // first chunk: decide whether to accept this upload at all
  {
    otaUpFail = false;
    otaUpErr = "upload failed";
    if (dashboardDeviceBusy())
    {
      otaUpFail = true;
      otaUpErr = "device busy - finish or leave the current screen first";
      Serial.println("[ota] upload refused: device busy");
      return;
    }
    // An image is already staged and we are only waiting for an idle moment to boot it.
    // Update.begin() here would ERASE the partition esp_ota_set_boot_partition() is already
    // pointing at, and if this second upload then fails, the pending restart boots a
    // half-written image. One image in flight at a time.
    if (otaRestartAt)
    {
      otaUpFail = true;
      otaUpErr = "a firmware is already staged - reboot into it first";
      Serial.println("[ota] upload refused: a staged image is waiting to boot");
      return;
    }
    if (!filename.endsWith(".bin"))
    {
      otaUpFail = true;
      otaUpErr = "not a .bin file";
      Serial.println("[ota] upload refused: not a .bin");
      return;
    }
    // UPDATE_SIZE_UNKNOWN: the multipart body carries no length we can trust, so let
    // the Update library size it against the free OTA partition instead.
    if (!Update.begin(UPDATE_SIZE_UNKNOWN))
    {
      otaUpFail = true;
      Update.printError(Serial);
      return;
    }
    // Optional ?md5=<32 hex>. Without it a TRUNCATED image still boots: Update.end(true)
    // sets _size = progress(), so "however many bytes arrived" counts as the whole image
    // and the half-written app is marked bootable. That is the one real brick path here.
    // With it, Update.end() compares the digest and refuses. Reject a malformed value
    // rather than ignoring it - a caller that asked for verification must not silently
    // get none.
    if (req->hasParam("md5"))
    {
      String md5 = req->getParam("md5")->value();
      // Update.end() compares the target against MD5Builder's output, which is always
      // lowercase, with a case-SENSITIVE String compare. PowerShell's Get-FileHash prints
      // uppercase - accepting it and then failing after the whole 2.3 MB had been uploaded
      // would be the least helpful possible way to reject a perfectly good image.
      md5.toLowerCase();
      bool hex = md5.length() == 32;
      for (size_t i = 0; hex && i < md5.length(); i++)
        hex = isxdigit((unsigned char)md5[i]);
      if (!hex || !Update.setMD5(md5.c_str()))
      {
        otaUpFail = true;
        otaUpErr = "md5 must be 32 hex characters";
        Update.abort();
        Serial.println("[ota] upload refused: bad md5 parameter");
        return;
      }
    }
    Serial.printf("[ota] upload begin: %s\n", filename.c_str());
  }

  if (otaUpFail)
    return; // keep draining the socket, but write nothing

  if (Update.write(data, len) != len)
  {
    otaUpFail = true;
    Update.printError(Serial);
    Update.abort();
    return;
  }

  if (final)
  {
    if (Update.end(true)) // true = the image is complete
      Serial.printf("[ota] upload done: %u bytes\n", (unsigned)(index + len));
    else
    {
      otaUpFail = true;
      Update.printError(Serial);
    }
  }
}

// Also called from updateFirmware() (NetworkTask) after a successful server OTA: same
// rule, one implementation.
void dashboardRequestRestart(uint32_t delayMs)
{
  otaRestartAt = millis() + delayMs;
  if (!otaRestartAt)
    otaRestartAt = 1; // 0 means "no restart pending"; millis() wraps every ~49 days
}

// Runs after the whole body has been consumed by handleOtaUpload().
static void handleOtaUploadDone(AsyncWebServerRequest *req)
{
  // A request that carried no upload at all must NOT be reported as a successful flash - it
  // would arm the reboot. This is reachable without any body: the route is registered with
  // WebServer.h's HTTP_POST (== 3), while AsyncWebServer matches with a BITWISE and against
  // its own flags, and GET is 1 - so `3 & 1` is non-zero and a plain `GET /otaupload` lands
  // here (GOTCHA 3, same arithmetic as the /wifilist bug).
  //
  // The check is PER-REQUEST on purpose. A static "an upload started" flag looks equivalent
  // and is not: an upload aborted mid-body (tab closed, WiFi blip during 2.3 MB) never
  // reaches this handler, so the flag would still be set when the next bare GET arrives -
  // which would then get 200 {"ok":true,"restarting":true} and an armed reboot having
  // flashed nothing. The multipart parser records the file field on the request itself
  // (WebRequest.cpp:540 emplaces it with isPost=true, isFile=true), so ask the request.
  //
  // ANY file part, not the name "firmware": onUpload fires for whichever file field arrives,
  // so a curl using -F "file=@fw.bin" really did get flashed (Update.end() already moved the
  // boot partition). Answering "no firmware in request" there would be a lie the next reboot
  // contradicts.
  bool gotFile = false;
  for (size_t i = 0; i < req->params() && !gotFile; i++)
  {
    const AsyncWebParameter *p = req->getParam(i);
    gotFile = p && p->isFile();
  }
  if (!gotFile)
  {
    req->send(400, "application/json", "{\"ok\":false,\"error\":\"no firmware in request\"}");
    return;
  }
  bool ok = !otaUpFail && !Update.hasError();
  AsyncWebServerResponse *res = req->beginResponse(
      ok ? 200 : 500, "application/json",
      ok ? String("{\"ok\":true,\"restarting\":true}")
         : String("{\"ok\":false,\"error\":\"") + otaUpErr + "\"}");
  // The reply must reach the browser BEFORE we reboot, hence the deferred restart below.
  res->addHeader("Connection", "close");
  req->send(res);
  if (ok)
    dashboardRequestRestart(); // dashboardLoop() reboots us once this passes, if idle
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

// /wifilist:
//   GET (or no param)          -> {max, nets:[{ssid, saved}]}. SSIDs only, never passwords.
//   POST ?remove=<ssid>        -> forget a saved network.
//   POST ?connect=<ssid>       -> switch to a saved network (reboot).
// The list is in NVS (Preferences), so these touch no EEPROM and are safe on AsyncTCP.
//
// Dispatched on the PRESENCE OF PARAMS, not req->method(). Registered as HTTP_ANY with a
// SINGLE handler on purpose: registering separate HTTP_GET and HTTP_POST handlers for one
// URI is broken in this build - GOTCHA 3 makes HTTP_GET/HTTP_POST sequential http_parser
// values (1 and 3), so AsyncWebServer's `_method & request->method()` = 1 & 3 = 1 != 0 and
// the first-registered (GET) handler wrongly grabs the POST. That is why an earlier
// GET+POST split silently ran the GET branch for every POST (forget/connect never fired).
static void handleWifiList(AsyncWebServerRequest *req)
{
  // Forget a saved network. Harmless anytime (no reboot), so not busy-guarded.
  if (req->hasParam("remove", true))
  {
    String s = req->getParam("remove", true)->value();
    bool hit = wifiStoreRemove(s);
    req->send(hit ? 200 : 404, "application/json",
              hit ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"not saved\"}");
    return;
  }
  // Switch the machine to a saved network. The password is looked up ON-DEVICE (never
  // sent to the browser) and the change is routed through SettingTask + reboot, exactly
  // like /wifi save - a runtime WiFi.begin() would deadlock async_tcp.
  if (req->hasParam("connect", true))
  {
    if (guardBusy(req)) // rebooting mid-run would destroy the sample
      return;
    String s = req->getParam("connect", true)->value();
    String pass;
    if (!wifiStoreGetPass(s, pass))
    {
      req->send(404, "application/json", "{\"ok\":false,\"error\":\"not saved\"}");
      return;
    }
    if (!_ForteSetting.postWifiCreds(s, pass)) // -> PEND_WIFI: trial + reboot
    {
      req->send(503, "application/json", "{\"ok\":false,\"error\":\"busy, retry\"}");
      return;
    }
    req->send(200, "application/json",
              String("{\"ok\":true,\"restarting\":true,\"seq\":") +
                  _ForteSetting.cfgSeq + "}");
    return;
  }
  req->send(200, "application/json", wifiStoreListJson());
}

// POST /deviceid?id=.. -> parameter.device_id, the one and only store (PEND_ID).
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
  // COUNTER is REUSED as the preheat round counter (eSensorPreheat increments it every ~20s
  // after boot, up to PREHEATLOOPS, then eSensormaintain leaves it parked there), so it is a
  // valid RUN length ONLY while actually amplifying. Everywhere else - idle, preheat, maintain,
  // finished, or a reviewed run - use the stored run length. Trusting getCurrentLoop() outside
  // eoptoreading made /curve report the ~15 preheat rounds at idle and TRUNCATE a reviewed
  // EEPROM run to that length (chart showed only its first ~15 points). See
  // docs/history/2026-07-22-curve-counter-preheat.md.
  // While amplifying COUNTER IS the truth: it is 0 for the first ~20 s (round 1 not done yet),
  // which correctly opens the live chart empty instead of inheriting the previous run.
  uint8_t n = (_displayCLD.type_infor == eoptoreading)
                  ? _sensor6035.getCurrentLoop()   // measuring: COUNTER = completed rounds
                  : _sensor6035.getLastRunLoops(); // else: stored run length (0 if never run)
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

// GET/POST /rename?slot=<0-9>&name=<disease>&sample=<label> -> persist.
// name and sample are independent: send either or both. name -> /slotnames.json,
// sample -> /slotsamples.json (two files, two arrays). Cap length so an unauthenticated
// LAN caller can't bloat NVS with a giant label.
static void handleRename(AsyncWebServerRequest *req)
{
  if (!req->hasParam("slot"))
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
  bool hasName = req->hasParam("name");
  bool hasSample = req->hasParam("sample");
  if (!hasName && !hasSample)
  {
    req->send(400, "application/json", "{\"ok\":false,\"error\":\"missing param\"}");
    return;
  }
  if (hasName)
  {
    String v = req->getParam("name")->value();
    if (v.length() > 32)
      v = v.substring(0, 32);
    slotNames[slot] = v;
    saveSlotLabels("names", slotNames);
  }
  if (hasSample)
  {
    String v = req->getParam("sample")->value();
    if (v.length() > 32)
      v = v.substring(0, 32);
    slotSamples[slot] = v;
    saveSlotLabels("samples", slotSamples);
  }
  req->send(200, "application/json", "{\"ok\":true}");
}

// STA joined OR the SoftAP fallback is up -> a browser can reach us.
static bool networkUp() { return WiFi.status() == WL_CONNECTED || apActive; }

// THE SoftAP SSID, and it has exactly ONE consumer: dashboardStartAP(), which hands it to
// WiFi.softAP(). Anything that REPORTS the name (screen_QR, buildHomeJson) reads
// WiFi.softAPSSID() instead - see those call sites.
//
// That split is the fix for two different bugs, in order:
//  - 2026-07-29: three places built the string by hand and drifted ("RAPID-" vs "FBT-"), so the
//    QR named a network the machine never raised. Fixed by making this the only builder.
//  - 2026-07-30: making them share a builder was still not enough, because WiFi.softAP() LATCHES
//    the name into the radio and this core has no API to change it in place. "What the SSID
//    should be" and "what is on the air" are different questions, and every consumer that
//    recomputed the first one was answering the wrong one after a device-ID change.
// So: build here, latch once, and never recompute for display.
//
// The clamp stays even though ForteSetting::sanitiseDeviceId() already guarantees 1..9 printable
// characters: 802.11 caps an SSID at 32 bytes (softAP() just refuses to start above that) and
// the QR encoder overflows a stack buffer rather than failing (see screen_QR). Both are one
// edit away from mattering again, and neither failure is visible in a build.
String dashboardApName()
{
  String id(protoID);
  if (id.length() == 0 || id.length() > 24 || id[0] != 'R')
    id = "RPL"; // never set ("UNSET"), or an ID that is not a RAPID serial
  return "FBT-" + id;
}

void dashboardStartAP()
{
  // Free the ~60KB of reserved Bluetooth memory: it's not needed in AP mode and,
  // left resident, starves the SoftAP's DHCP server + the dashboard -> clients
  // associate but never get an IP ("can't connect"). One-way; reboot restores BT/STA.
  releaseBluetoothStack();

  String ap = dashboardApName();
  WiFi.mode(WIFI_AP);
  // Open AP (no internet -> chart uses the bundled highcharts.js), capped at MAX_VIEWERS
  // devices: the extra one is refused while associating, so it never even gets an IP.
  WiFi.softAP(ap.c_str(), NULL, 1, 0, MAX_VIEWERS);
  apActive = true;
  // Captive portal: answer EVERY DNS query with our own IP. The phone's connectivity
  // probe then resolves to us, gets the redirect from onNotFound, and pops the dashboard
  // by itself - so scanning the QR (which only joins the WiFi) is enough, the user never
  // has to type 192.168.4.1. Served from dashboardLoop().
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(53, "*", WiFi.softAPIP());
  Serial.printf("[dash] SoftAP '%s' at http://%s/ | free=%u maxAlloc=%u\n",
                ap.c_str(), WiFi.softAPIP().toString().c_str(),
                ESP.getFreeHeap(), ESP.getMaxAllocHeap());
}

// Build a stable, DNS-safe hostname from the device ID (e.g. "RPL03010" -> "rpl03010"). Keeps
// only [a-z0-9-], lowercased; falls back to "rapid" if the ID has no usable chars. The
// dashboard is then reachable at http://<hostname>.local/ (mDNS) no matter the DHCP IP.
String dashboardHostname()
{
  String out;
  const char *id = protoID;
  for (size_t i = 0; id[i] && out.length() < 24; i++)
  {
    char c = id[i];
    if (c >= 'A' && c <= 'Z')
      out += (char)(c - 'A' + 'a');
    else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
      out += c;
    else if (c == '-' || c == '_' || c == ' ')
      out += '-';
  }
  while (out.length() && out[0] == '-')
    out.remove(0, 1); // a DNS label may not start with '-'
  if (out.length() == 0)
    out = "rapid";
  return out;
}

// ---- UI assets, baked into the firmware ------------------------------------------
// src/webAssets.h is generated from data/ by tools/pio_gzip_data.py. Serving from flash
// instead of LittleFS means firmware.bin is the ONLY artifact a unit needs, so the UI can
// ship over the same OTA path as the code (the U_SPIFFS path has no integrity check at
// all - see docs/plan/2026-07-28-ota-fleet-upgrade-243.md). RAM cost is zero: the response
// streams straight out of .rodata.
struct WebAsset
{
  const char *path;
  const uint8_t *data;
  size_t len;
  const char *mime;
  bool gz;
};

static const WebAsset kWebAssets[] = {
    // "/" and "/index.html" are the same page; serveStatic's setDefaultFile("index.html")
    // used to cover the first one.
    {"/", WEB_ASSET_INDEX_HTML_GZ, sizeof(WEB_ASSET_INDEX_HTML_GZ), "text/html", true},
    {"/index.html", WEB_ASSET_INDEX_HTML_GZ, sizeof(WEB_ASSET_INDEX_HTML_GZ), "text/html", true},
    {"/style.css", WEB_ASSET_STYLE_CSS_GZ, sizeof(WEB_ASSET_STYLE_CSS_GZ), "text/css", true},
    {"/script.js", WEB_ASSET_SCRIPT_JS_GZ, sizeof(WEB_ASSET_SCRIPT_JS_GZ), "application/javascript", true},
    {"/highcharts.js", WEB_ASSET_HIGHCHARTS_JS_GZ, sizeof(WEB_ASSET_HIGHCHARTS_JS_GZ), "application/javascript", true},
    {"/logo.png", WEB_ASSET_LOGO_PNG, sizeof(WEB_ASSET_LOGO_PNG), "image/png", false},
};

// NEVER pass a template processor to beginResponse(): _fillBufferAndProcessTemplates()
// scans the body for '%' and would rewrite gzip bytes in place.
static void sendAsset(AsyncWebServerRequest *req, const WebAsset &a)
{
  // Conditional GET. Dropping serveStatic drops the library's ETag/304 handling with it,
  // and without it every page load re-sends all 147 KB - on a device whose contiguous heap
  // is the scarce resource. One ETag for the whole set: they are versioned together.
  if (req->hasHeader("If-None-Match") && req->header("If-None-Match") == WEB_ASSETS_ETAG)
  {
    AsyncWebServerResponse *res = req->beginResponse(304);
    res->addHeader("ETag", WEB_ASSETS_ETAG);
    req->send(res);
    return;
  }
  AsyncWebServerResponse *res = req->beginResponse(200, a.mime, a.data, a.len);
  if (a.gz)
    res->addHeader("Content-Encoding", "gzip");
  // "no-cache" = may cache, MUST revalidate. Without it browsers apply heuristic freshness
  // and keep serving the OLD script.js after an update until a hard refresh.
  res->addHeader("Cache-Control", "no-cache");
  res->addHeader("ETag", WEB_ASSETS_ETAG);
  req->send(res);
}

// TEMPORARY (2026-07-27): print the largest CONTIGUOUS internal block at each boot step.
// That number - not free heap - decides whether the mbedTLS handshake fits (GOTCHA 2), and
// this board idles at 42 996 B where the BT-early-release fix measured 69 620 B. Free heap is
// 77 348 B, so ~26 KB is free-but-split: something is allocating in the middle of the big
// region. Printing per step says WHICH step. Remove once the culprit is pinned.
void dashHeapProbe(const char *where)
{
  Serial.printf("[heap] %-24s free=%u intLargest=%u\n", where, ESP.getFreeHeap(),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
}

void dashboardBegin()
{
  if (started)
    return;
  dashHeapProbe("dashboardBegin enter");

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
    // No LittleFS.begin() any more: the UI is in flash (kWebAssets) and the slot labels are
    // in NVS, so nothing here needs a filesystem. An empty, corrupt or never-formatted
    // spiffs partition - the most likely state of a unit coming from 2.4.2, which never
    // mounted LittleFS at all - now has no consequence whatsoever.
    // NEVER "fix" this with LittleFS.begin(true): that formats 1 572 864 B and calls
    // disableCore0WDT() (LittleFS.cpp:114-124) while ControlTask is driving heater duty.
    loadSlotLabels("names", slotNames);
    loadSlotLabels("samples", slotSamples);
    dashServer.addHandler(&viewerCap); // MUST precede dashEvents - see ViewerCapHandler
    dashServer.addHandler(&dashEvents);
    dashServer.on("/control", HTTP_ANY, controlHandler);
    dashServer.on("/home", HTTP_GET, [](AsyncWebServerRequest *req)
                  { req->send(200, "application/json", buildHomeJson()); });
    dashServer.on("/slots", HTTP_GET, handleSlots);
    dashServer.on("/errors", HTTP_GET, handleErrors);
    dashServer.on("/rename", HTTP_ANY, handleRename);
    dashServer.on("/curve", HTTP_GET, handleCurve);
    // Setting tab
    dashServer.on("/config", HTTP_GET, handleConfigGet);
    dashServer.on("/config", HTTP_POST, [](AsyncWebServerRequest *r) {}, NULL, handleConfigPost);
    // Commissioning check: is this machine actually RUNNING the v2.4.3a timings and thresholds,
    // or did the EEPROM copy quietly keep the old ones? Read-only and RAM-only - the config
    // revision byte is cached at boot precisely so this handler never opens EEPROM on AsyncTCP.
    dashServer.on("/selfcheck", HTTP_GET, [](AsyncWebServerRequest *req)
                  { req->send(200, "application/json", _ForteSetting.configSelfCheckJson()); });
    dashServer.on("/wifiscan", HTTP_GET, handleWifiScan);
    dashServer.on("/wifi", HTTP_POST, handleWifiSave);
    // HTTP_ANY (single handler), NOT separate GET+POST: see handleWifiList / GOTCHA 3 -
    // two handlers on one URI make the GET one wrongly grab the POST (1 & 3 != 0).
    dashServer.on("/wifilist", HTTP_ANY, handleWifiList);
    dashServer.on("/deviceid", HTTP_POST, handleDeviceId);
    dashServer.on("/calib", HTTP_POST, handleCalib);
    dashServer.on("/calib", HTTP_GET, handleCalib);
    // Result tab: reload the last completed run from EEPROM (review after reboot).
    dashServer.on("/reviewlast", HTTP_POST, handleReviewLast);
    dashServer.on("/ota", HTTP_ANY, handleOta); // one handler, dispatch on ?action (GOTCHA 3)
    // Upload a .bin straight from the browser: onRequest fires after the body is done,
    // onUpload receives it in chunks (see handleOtaUpload).
    dashServer.on("/otaupload", HTTP_POST, handleOtaUploadDone, handleOtaUpload);

    // UI assets LAST, same slot serveStatic used to hold. Handlers are tried in
    // registration order, so anything matching "/" must come after the API routes or a
    // file could shadow a route (GOTCHA 12).
    for (const WebAsset &a : kWebAssets)
      dashServer.on(a.path, HTTP_GET, [&a](AsyncWebServerRequest *req)
                    { sendAsset(req, a); });

    // Captive-portal catch-all (SoftAP only). Phones probe a known URL right after
    // joining (Android /generate_204, iOS /hotspot-detect.html, Windows /connecttest.txt);
    // our DNS answers those hostnames with our own IP, they land here, and the redirect
    // is what makes the OS pop the dashboard automatically - so scanning the WiFi QR is
    // all the user has to do. On STA this stays a plain 404 (no hijacking a real network).
    dashServer.onNotFound([](AsyncWebServerRequest *req)
                          {
      if (apActive) { req->redirect("http://" + WiFi.softAPIP().toString() + "/"); return; }
      req->send(404, "text/plain", "Not found"); });
    handlersReady = true;
    dashHeapProbe("after routes+static");
  }

  dashServer.begin();
  started = true;
  dashHeapProbe("after dashServer.begin");

  // Advertise http://<hostname>.local/ so the dashboard has a stable name when DHCP moves
  // the STA IP. Started ONCE (dashboardBegin re-runs after every suspend/resume). On SoftAP
  // the fixed 192.168.4.1 + captive portal already cover access, so only announce on STA.
  static bool mdnsUp = false;
  if (!mdnsUp && !apActive)
  {
    String hn = dashboardHostname();
    if (MDNS.begin(hn.c_str()))
    {
      MDNS.addService("http", "tcp", 80);
      mdnsUp = true;
      Serial.printf("[dash] mDNS up -> http://%s.local/\n", hn.c_str());
    }
    dashHeapProbe("after MDNS.begin");
  }

  IPAddress ip = apActive ? WiFi.softAPIP() : WiFi.localIP();
  Serial.printf("[dash] dashboard on http://%s/ (%s) | free=%u maxAlloc=%u\n",
                ip.toString().c_str(), apActive ? "AP" : "STA",
                ESP.getFreeHeap(), ESP.getMaxAllocHeap());
}

void dashboardLoop()
{
  // A new run cycle began -> the previous run's slot labels must not carry into it.
  //
  // Watched as a CONDITION here rather than hooked onto the transitions, for the same reason
  // as the QR block below: a cycle starts from the physical RED (button.cpp), the physical
  // BLUE lysis path, the web naming gate (/control?btn=ampname) AND a Serial command
  // (ForteSetting.cpp) - enumerating them leaves holes, and a hole here means a result
  // uploaded under the wrong disease name.
  //
  // isBusy() is exactly the not-running allowlist wanted: idle/finished/review/QR/setting/OTA
  // prompt are all false, everything that is part of a run (tube waits, naming gate, heating,
  // amplification, calib) is true. Clearing on the way INTO a run, not on the way out, is what
  // keeps the labels readable while the operator reviews the finished run on the Result tab.
  {
    static bool wasBusy = false; // boot lands on escreenStart -> no spurious wipe of a stored run
    bool busyNow = isBusy(_displayCLD.type_infor);
    if (!wasBusy && busyNow)
      dashboardClearSlotLabels();
    wasBusy = busyNow;
  }

  // A .bin was flashed via POST /otaupload. Reboot only now, so the 200 reply has had
  // time to leave the socket - restarting inside the handler drops it and the browser
  // reports a network error on a perfectly good update. But NOT if a run started in the
  // meantime: the new firmware is already staged in the OTA partition, so deferring the
  // reboot just means it activates at the next idle reboot instead of destroying the
  // sample now. (The upload handler only accepted the .bin while idle, but a physical
  // run could have started in the ~800 ms since.)
  //
  // "Not busy" is NOT enough on its own. escreenFinished is on the idle allowlist ("run done,
  // results on screen"), but it is also the state the WHOLE end-of-run pipeline runs under:
  // screen_Result('f') reads the run back from EEPROM, computes CT/outcome and then blocks
  // ~30-90 s in mbedTLS uploading to GAS + ingest + ERP. A reboot deferred through a 40 minute
  // run would otherwise fire within ~10 ms of the run ending - i.e. exactly onto the upload,
  // losing the results it was deferred to protect. `suspended` covers the upload window
  // itself; excluding escreenFinished covers the compute + CSV dump before it. The reboot
  // then lands when the operator dismisses the results (WHITE -> escreenStart).
  if (otaRestartAt && millis() > otaRestartAt && !dashboardDeviceBusy() && !suspended &&
      _displayCLD.type_infor != escreenFinished)
  {
    // Not only OTA any more: a device-ID change also queues this (ForteSetting PEND_ID /
    // JsonDataConfig) so the radio re-announces the new SSID / hostname / mDNS name.
    Serial.println("[dash] deferred restart");
    ESP.restart();
  }
  // Left the QR screen by ANY route -> undo the on-demand hotspot. Watched here rather than
  // hooked onto the WHITE handler: handleLongPress_Red/Blue/White each overwrite type_infor
  // from any state, so exit-by-exit arming left the machine stranded on its own hotspot with
  // STA dead, a run's upload silently failing, and nothing on the TFT saying so.
  //
  // Standing down when the operator comes BACK to the screen is why this keeps its own
  // deadline instead of arming dashboardRequestRestart() straight away: that flag is shared
  // with OTA and the device-ID change, and cancelling it would cancel theirs too.
  // The condition cannot survive the reboot it causes - apOnDemand is false on a fresh boot,
  // and the boot fallback never sets it - so this is not the reboot loop that a permanent
  // condition would be.
  if (apOnDemand)
  {
    static uint32_t apExitAt = 0;
    if (_displayCLD.type_infor == eShowQR)
      apExitAt = 0; // back on the QR screen - stand down
    else if (!apExitAt)
    {
      apExitAt = millis() + 1500;
      if (!apExitAt)
        apExitAt = 1; // 0 means "not armed"; millis() wraps every ~49 days
    }
    else if ((int32_t)(millis() - apExitAt) >= 0) // signed: survives the millis() wrap
    {
      apExitAt = 0;
      apOnDemand = false; // committed; don't re-arm on the next tick
      Serial.println("[dash] left the QR screen - rebooting out of the on-demand SoftAP");
      dashboardRequestRestart(0); // still gated on idle by the restart check above
    }
  }

  // Raise the SoftAP on request (Setting menu -> GREEN -> QR). Executed HERE, on NetworkTask,
  // because WiFi.mode() must not be re-entered from the button task while async_tcp is serving.
  // Consumed unconditionally so a request made while busy is dropped, not queued to fire later
  // at an arbitrary moment.
  if (apRequested)
  {
    apRequested = false;
    if (apActive)
    {
      // Already on the hotspot (boot fallback). Nothing to raise, and NOT marked on-demand:
      // the exit path must not reboot a machine that was going to be on the AP anyway.
      Serial.println("[dash] SoftAP already up - QR shows it as is");
    }
    else if (suspended || dashboardDeviceBusy())
    {
      // Killing STA mid-run takes the end-of-run upload with it. `suspended` is checked HERE
      // rather than by the early return below: that return would leave the flag latched, and
      // the hotspot would come up on the first tick after the upload finished - minutes after
      // the button was pressed, with nothing to connect the two. The QR then just shows the
      // STA address, which is still a usable way onto the dashboard.
      Serial.println("[dash] SoftAP request ignored - device busy or uploading");
    }
    else
    {
      Serial.println("[dash] SoftAP requested from the setting menu");
      dashboardStartAP();
      apOnDemand = true;
      // The QR was drawn before the radio switched, so it still shows the STA URL. Ask the
      // display to paint it again now that dashboardIsAP() answers differently.
      if (_displayCLD.type_infor == eShowQR)
        _displayCLD.changeScreen = true;
    }
  }

  // Ask the server for a new build every OTA_POLL_MS. Before v2.4.4 checkFirmware() ran
  // exactly ONCE per boot (main.cpp, ~2 s after power-on) and never again, so a machine
  // that missed its DHCP lease in that window stayed on an old build until someone
  // power-cycled it - the single biggest reason OTA "went quiet" in the field.
  //
  // Deliberately NOT calling checkFirmware() here: it blocks for seconds inside mbedTLS
  // and this is NetworkTask, which also pumps the dashboard. It goes through the SAME
  // queue the web Setting button uses (PEND_OTACHECK -> SettingTask), whose drainPending()
  // already refuses to run while a run is in progress.
  //
  // Own deadline rather than reading otaLastCheck: that only advances when a GET actually
  // COMPLETES, so a machine with no route to the server would re-queue on every tick.
  {
    static const uint32_t OTA_POLL_MS = 6UL * 60 * 60 * 1000; // 6 h
    static uint32_t nextOtaPoll = OTA_POLL_MS;                // first poll 6 h after boot
    // Gated with the REBOOT-grade predicate, not the settings one. drainPending()'s
    // dashboardDeviceBusy() is not enough here: escreenFinished counts as IDLE there, but it
    // is the state the whole end-of-run pipeline runs under (screen_Result('f') spends ~8 s in
    // getDataAmplificationEEPROM + ~1.2 s dumping CSV before dashboardSuspend() is even
    // called, then 30-90 s of TLS). Two ways that bites: a second mbedTLS session opens
    // against the ~42 KB contiguous budget of GOTCHA 2, and - worse - a successful check
    // writes type_infor = eUpdateOTA, which is on the idle allowlist, so an armed otaRestartAt
    // passes the stricter gate above and reboots into the middle of the result computation.
    // The deadline stays expired, so nothing is skipped; the poll just fires a tick later.
    if (!apActive && WiFi.status() == WL_CONNECTED && !suspended &&
        _displayCLD.type_infor != escreenFinished &&
        (int32_t)(millis() - nextOtaPoll) >= 0) // signed: survives the millis() wrap
    {
      nextOtaPoll = millis() + OTA_POLL_MS; // re-arm even if the queue is full
      if (!nextOtaPoll)
        nextOtaPoll = 1;
      // prompt=true: this is the ONLY thing that puts the eUpdateOTA screen up outside of
      // boot, and that screen is where RED means "install". Queue it without and the poll
      // is invisible to everyone except whoever happens to open the dashboard.
      // drainPending() runs it only while idle, so the takeover cannot land on a run.
      if (otaState == OTA_IDLE || otaState == OTA_FAILED)
        _ForteSetting.postOtaCheck(true); // AVAILABLE/UPDATING/ACCEPTED: already in flight
    }
  }

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
        // setup() already tried every saved network (connectSavedNetworks). If none came
        // up in the grace window either, raise the hotspot. No WiFi.begin() here - runtime
        // begin() from a task deadlocks async_tcp forever (test_no_runtime_wifi_begin.py).
        Serial.printf("[wifi] STA not up after %lu s -> SoftAP fallback\n",
                      (unsigned long)(STA_GRACE_MS / 1000));
        dashboardStartAP();
      }
      return;
    }
    dashboardBegin();
  }

  // Captive-portal DNS: must run EVERY tick (~10 ms), i.e. above the 1 s push throttle
  // below - a phone's connectivity probe gives up long before 1 s, and then the portal
  // never pops. Cheap no-op when the query queue is empty; only armed while apActive.
  if (apActive)
    dnsServer.processNextRequest();

  uint32_t now = millis();
  if (now - lastPush < 1000)
    return;
  lastPush = now;

  // NOTE: no runtime network SWITCHING here, by design. If the joined AP is switched off,
  // WiFi.setAutoReconnect(true) keeps re-joining THAT SSID in the background and rejoins it
  // the moment it returns - self-healing, no reboot. Failing over to a DIFFERENT saved
  // network would need WiFi.begin() at runtime, which deadlocks async_tcp forever (even
  // with dashboardSuspend first - see GOTCHA 8 Option C). So switching networks only
  // happens at boot (connectSavedNetworks): power-cycle to re-pick. The machine stays
  // fully usable offline meanwhile; the TFT shows "Scanning..." so the state is visible.

  // Heap watch (every 10 s) so you can measure load, esp. in AP mode.
  static uint32_t lastHeap = 0;
  if (now - lastHeap > 10000)
  {
    lastHeap = now;
    // intLargest = largest CONTIGUOUS block in INTERNAL RAM - this (not maxAlloc, which can
    // count PSRAM) is what mbedTLS must fit its ~32KB handshake buffers into (-32512 when
    // it can't). intFree = total internal free. Diagnosing the upload -32512 (GOTCHA 2).
    Serial.printf("[dash] heap free=%u maxAlloc=%u intFree=%u intLargest=%u clients=%u ap=%d\n",
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap(),
                  heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                  dashEvents.count(), apActive);
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

// dashboardEnd() was DELETED on 2026-07-29: Wifi_Connect() was its only caller and went with
// WiFiManager. Nothing else ever wants the dashboard permanently down - dashboardSuspend() /
// dashboardResume() is the reversible pair the upload path uses.

// Temporarily free the dashboard's network heap (close SSE clients + stop the
// server) so a TLS upload gets its ~32KB contiguous block. Without this, an open
// SSE socket + AsyncWebServer fragment the heap and mbedTLS fails with -32512.
// dashboardLoop() stays idle while suspended so NetworkTask can't restart it
// mid-upload; dashboardResume() lets it come back on the next tick.
void dashboardSuspend()
{
  // The server is left RUNNING through the upload. It used to be stopped here to free heap
  // for the mbedTLS handshake, but that stop is what killed the dashboard afterwards:
  //
  //   dashServer.end() closes only the LISTEN pcb - the SSE client sockets stay established
  //   on port 80 (closing them self-deadlocks, GOTCHA 14). lwIP's tcp_bind refuses a port
  //   that any active pcb still holds, so the dashServer.begin() on resume failed with
  //   "AsyncTCP begin(): bind error: -8" and the dashboard never came back until a reboot.
  //
  // And the heap reason is gone: trimming the six task stacks (2026-07-27) gave back
  // 23 556 B free / 16 384 B contiguous, so the upload now completes on the first attempt
  // with intLargest = 69 620 against a handshake that needs 32 768-36 352. Starving
  // async_tcp during the ~15 s of TLS is already accounted for -
  // CONFIG_ASYNC_TCP_USE_WDT=0 exists for exactly this window (GOTCHA 11).
  //
  // `suspended` still stops dashboardLoop() pushing SSE events while DisplayTask is blocked
  // in mbedTLS; that part costs nothing and keeps the event queue from backing up.
  suspended = true;
}

void dashboardResume()
{
  suspended = false;
}
