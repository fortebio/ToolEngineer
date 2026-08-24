#include "Bluetooth.h"
#include "sensor6035.h"
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp32-hal.h>
#include "errorCheck.h"
#include "webDashboard.h"
#include "esp_heap_caps.h" // heap_caps_get_largest_free_block: gate TLS on a big internal block
#include "secrets.h"       // upload endpoints + tokens (GITIGNORED; copy from secrets.example.h)

BluetoothSerial SerialBT;
volatile bool gBtReleased = false; // see releaseBluetoothStack() / define.h
String ssid = "";
String password = "";
uint64_t epsid = ESP.getEfuseMac();
String id(String(epsid).c_str());
// id_device (the second device-ID store) was DELETED on 2026-07-30. parameter.device_id, via
// the protoID macro, is the only one. See loadSettingDevice().

const char *serverName = SECRET_GAS_URL;
const char *serverName2 = SECRET_INGEST_URL;
const char *server_engineerToken = SECRET_INGEST_TOKEN;

const char *serverERP = SECRET_ERP_URL;
const char *server_erpToken = SECRET_ERP_TOKEN;

/***********************************************************************
 * Function: connectBLE()
 * Description: Initializes the Bluetooth Classic SerialBT interface with a
 *  device name of "RAPID PLUS -<EfuseMac>". Skips begin() if the BT stack
 *  has already been permanently released (gBtReleased) to avoid asserting
 *  on a dead stack.
 * pramameter: none
 *  return: none
 */
void connectBLE()
{
  // Once releaseBluetoothStack() has freed the controller memory, the stack
  // cannot be re-initialised without a reboot. The only caller (eSettingBluetooth)
  // does ESP.restart() right after, so just skip begin() to avoid asserting on
  // a dead stack; BT comes back fresh after the restart.
  if (gBtReleased)
    return;
  SerialBT.begin("RAPID PLUS -" + String(ESP.getEfuseMac())); // Bluetooth device name
}

/***********************************************************************
 * Function: readEEPROM()
 * Description: Dumps the entire EEPROM contents as a hexadecimal string,
 *  printing 32-byte (64 hex char) rows via info_displayln for debugging.
 * pramameter: none
 *  return: none
 */
void readEEPROM()
{
  EEPROM.begin(_EEPROM_SIZE);
  String strjson = "";
  // Read json data from EEPROM
  char tmp[4] = {0};
  for (uint32_t i = 0; i < _EEPROM_SIZE; i++)
  {
    char c = EEPROM.read(i);
    sprintf(tmp, "%02X", c);
    strjson += tmp;
    if (i % 32 == 31)
    {
      info_displayln(strjson);
      strjson = "";
    }
  }
  info_displayln(strjson);
  EEPROM.end();
}

/***********************************************************************
 * Function: paraDisplay()
 * Description: Serializes a parastructure (calibration slopes/origins, LED
 *  power, opto parameters, PID values, temperature offsets, buzzer state,
 *  kitId, etc.) into a pretty-printed JSON document and outputs it (suffixed
 *  with '@') via info_displayln.
 * pramameter: para - the parameter structure whose fields are to be displayed
 *  return: none
 */
/***********************************************************************
 * Function: paraToJson()
 * Description: Serialises the whole parameter struct into the SAME JSON shape
 *  ForteSetting::JsonDataConfig() parses, so a client can GET this, edit a field
 *  and POST it back. The web Setting tab uses it for GET /config.
 *  Note the shape is not uniform: slopes/origins are nested under
 *  "opto calibration" and the algorithm fields under "parameters", while
 *  LED power / PID / temps / buzzer / kitId are top-level. Keep it in step with
 *  the containsKey blocks in ForteSetting.cpp - they are the contract.
 * pramameter: para - the struct to serialise
 *  return: String - the JSON document
 */
String paraToJson(parastructure para)
{
  JsonDocument paradata; // ArduinoJson 7 sizes this on demand - no capacity to tune

  paradata["device ID"] = para.device_id;
  paradata["para version"] = para.para_version;
  paradata["PCB version"] = para.PCB_version;
  JsonObject calibration = paradata["opto calibration"].to<JsonObject>();
  JsonArray slopes = calibration["slopes"].to<JsonArray>();
  JsonArray origins = calibration["origins"].to<JsonArray>();
  JsonArray ledPower = paradata["LED power"].to<JsonArray>();
  for (int i = 0; i < OPTOCHANNELS; i++)
  {
    slopes.add(para.slopes[i]);
    origins.add(para.origins[i]);
    ledPower.add(para.led_power[i]);
  }

  JsonObject opto_parameter = paradata["parameters"].to<JsonObject>();
  opto_parameter["min increase"] = para.min_increase;
  opto_parameter["min sharpness"] = para.min_sharpness;
  opto_parameter["min slight positive time"] = para.min_slight_positive_time;
  opto_parameter["detect shape"] = para.detect_shape;
  opto_parameter["detection margin time"] = para.detection_margin_time;
  opto_parameter["arm percentile"] = para.arm_percentile;
  opto_parameter["transition percentile"] = para.transition_percentile;
  opto_parameter["sg order"] = para.sg_order;
  opto_parameter["sg window"] = para.sg_window;
  opto_parameter["baseline start"] = para.baseline_start;
  opto_parameter["baseline range"] = para.baseline_range;

  paradata["units"] = para.units;
  paradata["lysis duration"] = para.lysisDuration;
  paradata["opto preheat time"] = para.optopreheatduration;
  paradata["LED Duration"] = para.LEDDuration;
  paradata["time per loop"] = para.timePerLoop;
  paradata["amplification time"] = para.amplification_time;
  paradata["lysis temperature"] = para.lysisTemp;
  paradata["amplification temperature"] = para.amplifTemp;

  JsonArray bottomTemperatureSensorSq = paradata["bottom temperature sensor seq"].to<JsonArray>();
  for (int i = 0; i < 3; i++)
  {
    bottomTemperatureSensorSq.add(para.bottomTemperatureSensorSq[i]);
  }

  JsonArray topTemperatureSensorSq = paradata["top temperature sensor seq"].to<JsonArray>();
  for (int i = 0; i < 3; i++)
  {
    topTemperatureSensorSq.add(para.topTemperatureSensorSq[i]);
  }

  JsonArray pid1 = paradata["PID parameter"].to<JsonArray>();
  JsonArray pid2 = paradata["PID2 parameter"].to<JsonArray>();
  JsonArray pid3 = paradata["PID3 parameter"].to<JsonArray>();
  JsonArray bottomOverheat = paradata["Bottom overheat value"].to<JsonArray>();
  JsonArray topOverheat = paradata["Top overheat value"].to<JsonArray>();
  for (int i = 0; i < 3; i++)
  {
    pid1.add(para.kpid[i]);
    pid2.add(para.kpid2[i]);
    pid3.add(para.kpid3[i]);
    bottomOverheat.add(para.bottomOverheat[i]);
  }

  for (int i = 0; i < 2; i++)
  {
    topOverheat.add(para.topOverheat[i]);
  }

  JsonArray temperatureOffset = paradata["temperature value calibration"].to<JsonArray>();
  for (int i = 0; i < 6; i++)
  {
    temperatureOffset.add(para.temperatureOffset[i]);
  }

  // hotlidPWM is settable via the "top heater PWM" key (ForteSetting.cpp) but was
  // never emitted here, so a GET could not pre-fill it. Shape: [[low,high],[low,high]]
  // to match hotlidPWM[i][0]=low / [i][1]=high as the PIDControl macros read them.
  JsonArray topPWM = paradata["top heater PWM"].to<JsonArray>();
  for (int i = 0; i < 2; i++)
  {
    JsonArray row = topPWM.add<JsonArray>();
    row.add(para.hotlidPWM[i][0]);
    row.add(para.hotlidPWM[i][1]);
  }

  paradata["buzzer"] = para.buzzerOn ? "On" : "Off";
  paradata["kitId"] = para.kitId;

  String output;
  serializeJson(paradata, output);
  return output;
}

void paraDisplay(parastructure para)
{
  info_displayf("Length: %d\n", para.length);
  info_displayln(paraToJson(para) + "@");
}

/***********************************************************************
 * Function: loadParaFromEEPROM()
 * Description: Reads a parastructure from EEPROM at PARAMETERPOS and, if its
 *  stored length matches sizeof(para) (indicating valid data), displays it
 *  via paraDisplay(); otherwise reports that no parameters are present.
 * pramameter: none
 *  return: none
 */
void loadParaFromEEPROM()
{
  EEPROM.begin(_EEPROM_SIZE);
  parastructure para;
  EEPROM.get(PARAMETERPOS, para);
  info_displayf("check para in EEPROM, length is %d, right one is %d\n", para.length, sizeof(para));
  if (para.length == sizeof(para)) // if the length of the parameter in EEPROM is not -1 or 0, then use it.
  {
    info_displayln("There is para in the EEPROM");
    paraDisplay(para);
  }
  else
  {
    info_displayln("No para in the EEPROM");
    return;
  }
}

/***********************************************************************
 * Function: saveSettingDevice()
 * Description: Persists the WiFi SSID and password to EEPROM.
 *
 *  It no longer writes the device ID (ADDR_ID_DEVICE_BASE) - that second store is gone, see
 *  loadSettingDevice(). The ADDR_CHECK_ID_DEVICE flag went with it: its only reader was the
 *  WiFiManager captive portal, deleted 2026-07-29.
 * pramameter: none
 *  return: none
 */
void saveSettingDevice()
{
  eepromLock();
  EEPROM.begin(_EEPROM_SIZE);
  EEPROM.writeString(ADDR_SSID, ssid);
  EEPROM.writeString(ADDR_PASSWORD, password);
  EEPROM.commit();
  EEPROM.end();
  eepromUnlock();
}

/***********************************************************************
 * Function: loadSettingDevice()
 * Description: Loads the WiFi SSID and password from EEPROM into the globals.
 *
 *  NO LONGER LOADS THE DEVICE ID (2026-07-30). The ID used to live here too, in a second
 *  store at ADDR_ID_DEVICE_BASE mirrored into a global `id_device` - and the two stores drifted
 *  in every direction: the web header read one while the Setting card read the other, and
 *  Serial/BT could change one without the other. There is now exactly ONE store,
 *  parameter.device_id, read through the protoID macro, validated once in
 *  ForteSetting::sanitiseDeviceId(). Slot 170..210 in the EEPROM layout is dead - do NOT start
 *  writing it again "for compatibility": a second copy is what caused the bug.
 * pramameter: none
 *  return: none
 */
void loadSettingDevice()
{
  EEPROM.begin(_EEPROM_SIZE);
  ssid = EEPROM.readString(ADDR_SSID);
  password = EEPROM.readString(ADDR_PASSWORD);
  EEPROM.end();
}

/**
 * @brief Idempotent teardown of the Bluetooth Classic stack.
 *
 * esp_bt_mem_release() PERMANENTLY hands the controller + bluedroid memory
 * (~60KB) back to the general heap and may only be called ONCE: a second call
 * re-adds the already-freed regions and corrupts the heap. Just as bad, any
 * SerialBT call after deinit posts to a freed bluedroid thread and trips
 * `assert failed: osi_thread_post (thread != NULL)` -> reboot.
 *
 * Three flows tear BT down (main.cpp at boot, auto upload in screen_Result, and
 * manual upload) and none is guaranteed to end in a restart, so
 * they can run one after another within a single power cycle. The gBtReleased
 * guard makes this safe to call from any of them in any order, and lets the
 * info_display* macros + SettingTask stop touching SerialBT afterwards.
 */
/***********************************************************************
 * Function: releaseBluetoothStack()
 * Description: Idempotent, one-time teardown of the Bluetooth Classic stack:
 *  ends SerialBT, disables/deinits bluedroid and the BT controller, and
 *  permanently releases the controller memory via esp_bt_mem_release. Sets
 *  gBtReleased last so no further SerialBT access occurs; returns early if
 *  already released.
 * pramameter: none
 *  return: none
 */
void releaseBluetoothStack()
{
  if (gBtReleased)
    return;

  SerialBT.end();
  if (esp_bluedroid_get_status() != ESP_BLUEDROID_STATUS_UNINITIALIZED)
  {
    esp_bluedroid_disable();
    esp_bluedroid_deinit();
  }
  if (esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_IDLE)
  {
    esp_bt_controller_disable();
    esp_bt_controller_deinit();
  }
  esp_bt_mem_release(ESP_BT_MODE_BTDM);

  // Set LAST: from here on, no further SerialBT access is allowed.
  gBtReleased = true;
}

// Wifi_Connect() (tzapu/WiFiManager captive portal) was DELETED on 2026-07-29. Everything it
// did now lives in the web dashboard, which is reachable without it: STA fail -> SoftAP
// (dashboardApName()) + captive portal -> Setting tab. Credentials: POST /wifi + /wifilist
// (wifiStore.cpp, trial-then-commit). Device ID: POST /deviceid. Firmware: /ota + /otaupload.
// Dropping the library saved 93 952 B of flash (73.6% -> 70.8%) and removed three hazards it
// carried: a disableCore0WDT() that was never re-enabled while ControlTask drives heater duty,
// a resetSettings() that wiped the core's stored credentials on every visit, and its own SoftAP
// which tore down dashboardEnd() + the AP fallback the operator's phone was already on.

/***********************************************************************
 * Function: getDataAmplificationEEPROM()
 * Description: Reads the stored amplification record array (10 x 130 Words)
 *  from EEPROM at RECORDPOS and copies it into _sensor6035.sensor67Value.
 * pramameter: none
 *  return: none
 */
void getDataAmplificationEEPROM(void)
{
  // Serialize vs a concurrent error-save (ControlTask) that would double-free the shared
  // 4KB EEPROM buffer mid-read and hand back garbage -> /reviewlast probe fails (GOTCHA 2).
  eepromLock();
  EEPROM.begin(_EEPROM_SIZE);
  Word tmp[10 * 130] = {0};
  EEPROM.get(RECORDPOS, tmp);
  memcpy(_sensor6035.sensor67Value, tmp, sizeof(tmp));
  EEPROM.end();
  eepromUnlock();
}

/***********************************************************************
 * Function: rounded()
 * Description: Rounds a floating-point value to one decimal place.
 * pramameter: value - the float value to round
 *  return: float - the value rounded to 1 decimal place
 */
float rounded(float value)
{
  return round(value * 10.0) / 10.0f; // Round to 1 decimal place
}

// Read an HTTP response body with our OWN idle deadline, because HTTPClient::getString()
// has none. getString() -> writeToStreamDataBlock() loops
//   while (connected() && (len > 0 || len == -1)) { if (available()) {...} else delay(1); }
// with NO timeout on the else branch. The ingest endpoint answers with Connection: close +
// no Content-Length (so len == -1) but its reverse-proxy keeps the TLS socket open without
// sending FIN, so connected() stays true and available() stays 0 -> the DisplayTask spins
// on delay(1) FOREVER (device hangs, power-cycle only; the configured setTimeout/handshake
// timeouts do not cover this loop). This bounds the read at `idleMs` of no new bytes.
static String readBodyDeadlined(HTTPClient &http, uint32_t idleMs)
{
  String body;
  WiFiClient *s = http.getStreamPtr();
  if (!s)
    return body;
  uint32_t last = millis();
  while (http.connected() && millis() - last < idleMs)
  {
    while (s->available())
    {
      body += (char)s->read();
      last = millis(); // reset the deadline on every byte received
    }
    delay(1);
  }
  return body;
}

// POST `payload` as JSON to `url` over TLS, retrying transient failures. Each attempt
// builds a FRESH, scoped WiFiClientSecure so the previous connection is fully torn down
// first: reusing one client across the two upload hosts - or across rapid back-to-back
// "Up Data" presses - leaves a half-closed TLS socket that the next connect hits as
// code=-1 (connection refused) / code=-3 (send payload failed). Only network-level
// failures (code <= 0) and 5xx are retried; a 4xx (e.g. 401 bad token) is permanent, so
// retrying it would just waste the dashboard-suspended window. `label` tags the serial
// markers. Auth (each host differs): `bearer` -> "Authorization: Bearer <bearer>" (ingest);
// `apiKey` -> "X-API-Key: <apiKey>" (ERP); pass nullptr for hosts that need neither (GAS).
// `outBody` gets the (deadline-bounded) response body for logging. Returns the HTTP/error code.
static int postJsonRetry(const char *url, const String &payload, const char *label,
                         const char *bearer, const char *apiKey, String &outBody)
{
  const uint8_t ATTEMPTS = 4;
  // mbedTLS needs a big CONTIGUOUS block of INTERNAL RAM for its handshake buffers; on a
  // heap-tight board the previous TLS leaves the largest free block below that -> -32512.
  const size_t TLS_MIN = 33 * 1024;
  int code = 0;
  for (uint8_t a = 1; a <= ATTEMPTS; a++)
  {
    // Heap gate: wait (up to ~2.5s) for the internal largest-free-block to recover to
    // TLS_MIN before opening the socket, so the handshake gets its contiguous buffers. The
    // block grows as the previous attempt's mbedTLS memory frees + coalesces.
    size_t intLargest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    uint32_t gate0 = millis();
    while (intLargest < TLS_MIN && millis() - gate0 < 2500)
    {
      delay(150);
      intLargest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    }

    WiFiClientSecure client; // fresh per attempt -> no stale/half-closed socket reuse
    client.setInsecure();    // skip cert chain -> smaller mbedTLS allocation
    client.setTimeout(60);
    client.setHandshakeTimeout(30);

    HTTPClient http;
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS); // GAS 302 == success for us
    http.setReuse(false);
    http.useHTTP10(true);
    if (!http.begin(client, url))
    {
      Serial.printf("[up] %s begin failed (try %u/%u)\n", label, a, ATTEMPTS);
      code = -1;
      delay(400);
      continue;
    }
    http.setTimeout(60000); // header-phase read timeout (GAS emits its 302 after ~6-40s)
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Connection", "close");
    if (bearer)
      http.addHeader("Authorization", String("Bearer ") + bearer);
    if (apiKey)
      http.addHeader("X-API-Key", apiKey);

    Serial.printf("[up] %s POST begin (free=%u intLargest=%u try %u/%u)\n",
                  label, ESP.getFreeHeap(), (unsigned)intLargest, a, ATTEMPTS);
    uint32_t t0 = millis();
    code = http.POST(payload);
    uint32_t dt = millis() - t0;
    outBody = readBodyDeadlined(http, 5000); // bounded: getString() alone never times out
    http.end();

    // 2xx = direct success; 302 from GAS = script accepted and processed the data.
    bool ok = (code >= 200 && code < 300) || code == HTTP_CODE_FOUND;
    if (ok)
    {
      Serial.printf("[up] %s POST OK in %u ms, code=%d (try %u)\n", label, dt, code, a);
      return code;
    }
    // Retry ONLY connection-level failures where the request did NOT complete at the
    // server: -1 (connection refused / TLS handshake) or -3 (send payload failed), plus
    // 5xx. Do NOT retry -11 (read Timeout): the body was fully sent, so GAS - which
    // appends its row BEFORE emitting the 302 - has very likely already PROCESSED it, and
    // a retry would DUPLICATE the row. It is also pointless when the peer is merely slow /
    // rate-limiting (GAS under many rapid uploads): each retry just burns another full
    // 60s. A timed-out GAS is treated as "probably done, move on to ingest".
    bool retryable = (code == -1) || (code == -3) || (code >= 500);
    Serial.printf("[up] %s POST FAIL in %u ms, code=%d (%s) try %u/%u%s\n",
                  label, dt, code, http.errorToString(code).c_str(), a, ATTEMPTS,
                  retryable ? "" : " [no retry]");
    if (!retryable)
      return code;
    delay(600); // let the socket + TLS stack settle before a fresh attempt
  }
  return code;
}

/***********************************************************************
 * Function: postJsonToAllTargets()
 * Description: POSTs one JSON payload to ALL THREE cloud destinations - GAS/Google Sheet,
 *  the ingest API (Bearer) and the ERP (X-API-Key). Each gets a FRESH TLS client and its own
 *  retries (postJsonRetry); reusing one WiFiClientSecure across hosts leaves a half-closed
 *  socket that the next connect hits as code=-1/-3. Returns the GAS code, which is the one
 *  the TFT reports as "Upload Success/Failed".
 *
 *  THE POINT OF THIS FUNCTION IS THAT THE DESTINATION LIST EXISTS ONCE. The results path and
 *  the error path each had their own copy and they drifted: errors only ever reached GAS, so
 *  a machine that failed a channel told the spreadsheet and told neither of the two systems
 *  anyone actually watches. Add a fourth destination here and both paths get it.
 *
 *  Does NOT suspend the dashboard - the caller owns that window, because it also covers
 *  building the payload (postData_GoogleSheet frees a ~25-40 KB JsonDocument before the
 *  handshake, and that has to happen inside the same suspended stretch).
 * pramameter: jsonPost - the serialized payload
 * pramameter: what - short tag for the serial markers ("data" / "error")
 *  return: the GAS HTTP/error code
 */
uint16_t postJsonToAllTargets(const String &jsonPost, const char *what)
{
  Serial.printf("[up] %s -> GAS + ingest + ERP (%u B)\n", what, (unsigned)jsonPost.length());

  // GAS /exec answers 302 after it finishes appending (~6-40 s); postJsonRetry treats that
  // as success.
  String gasBody;
  uint16_t gasCode = postJsonRetry(serverName, jsonPost, "GAS", nullptr, nullptr, gasBody);

  delay(100);

  String server_feedback;
  postJsonRetry(serverName2, jsonPost, "ingest", server_engineerToken, nullptr, server_feedback);
  Serial.printf("Engineer server feedback: %s\n", server_feedback.c_str());

  delay(100);

  // ERP (api.fortebio.tech) authenticates with an X-API-Key header (NOT Bearer).
  String erp_feedback;
  postJsonRetry(serverERP, jsonPost, "ERP", nullptr, server_erpToken, erp_feedback);
  Serial.printf("ERP server feedback: %s\n", erp_feedback.c_str());

  return gasCode;
}

/***********************************************************************
 * Function: postData_GoogleSheet()
 * Description: Builds a JSON payload of machine specs, per-slot CT values,
 *  results, peak features/outcomes and raw amplification curves, then POSTs
 *  it over HTTPS (TLS insecure) to the Google Apps Script endpoint. Frees the
 *  JSON document before the TLS handshake to avoid heap fragmentation, and
 *  treats a 2xx or 302 redirect as success. Skips if WiFi is not connected.
 * pramameter: CT_value - array of 10 CT values per slot
 * pramameter: result - array of 10 result characters per slot (P/N/S/E)
 * pramameter: loops - number of amplification loops (raw data points per slot)
 *  return: none
 */
uint16_t postData_GoogleSheet(float CT_value[10], char result[10], uint8_t loops)
{
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("postData_GoogleSheet: WiFi not connected, skip");
    return 0;
  }

  Serial.println("[up] begin: suspend dashboard + build JSON");

  // Free the dashboard's network heap (close SSE + stop AsyncWebServer) so mbedTLS
  // can allocate its contiguous handshake buffers. Otherwise: -32512 (SSL memory
  // allocation failed). dashboardResume() before every return below.
  dashboardSuspend();
  Serial.println("[up] suspended"); // marker: past dashboardSuspend (SSE-close deadlock site)

  // Precondition for the TLS handshake below: the Bluetooth Classic stack must be
  // fully released so mbedTLS can allocate its ~40KB contiguous handshake buffers.
  // Idempotent (gBtReleased guard) — safe even if the caller already released BT.
  releaseBluetoothStack();

  struct DiagnosticOutcome outcome[10];
  struct FeatureDetection peak_features[10];

  /* Calculate CT_value and result */
  bool flag = _sensor6035.bResultPutToGoogleSheet(CT_value, result, outcome, peak_features);
  Serial.println("[up] result computed"); // marker: past the detection algorithm

  /* PUBLISH TO THE WEB HERE - the instant the numbers exist, not after the upload.
   *
   * screen_Result() used to call dashboardSetResults() only AFTER this function returned, and
   * this function blocks for up to ~90 s (three TLS endpoints, GAS alone answers in 6-40 s).
   * For that whole window at the end of a 40-minute run the dashboard reported
   * /slots ready=false and /curve count=0, so a Result tab opened right then drew an empty
   * chart. Worse, the client reacts to ready=false by POSTing /reviewlast, which SettingTask
   * refuses to drain while the device is busy uploading - so its 15 s poll expired and the
   * tab stayed blank until a manual reload. Publishing first closes that window entirely.
   *
   * Safe to do while "suspended": dashboardSuspend() no longer stops the server (see
   * webDashboard.cpp), it only pauses SSE pushes, so /slots and /curve keep answering.
   *
   * Both caches together, never one alone: /slots reads gResultsReady and /curve reads
   * lastRunLoops, and publishing one without the other is what produces a filled table above
   * an empty chart. Length comes from scanning the record, never from amplification_time. */
  dashboardSetResults(CT_value, result);
  _sensor6035.setLastRunLoops(_sensor6035.scanRunLength());

  // Build JSON inside a nested scope so the JsonDocument is destructed
  // (and its ~25-40KB internal pool freed) BEFORE we open the TLS socket.
  // mbedTLS needs a big contiguous free block; building the doc and the
  // serialized String at the same time as the TLS handshake causes
  // X509 alloc failures (-10368) on a fragmented heap.
  String jsonPost;
  {
    JsonDocument dataPostGoogleSheet;

    dataPostGoogleSheet["method"] = "append";
    dataPostGoogleSheet["id_device"] = String(_ForteSetting.parameter.device_id);
    dataPostGoogleSheet["version"] = FirmwareVer;
    dataPostGoogleSheet["kitId"] = String(_ForteSetting.parameter.kitId);
    if (_displayCLD.type_infor == eUpLoadData)
    {
      dataPostGoogleSheet["type_Upload"] = "Manual";
    }
    else if (_displayCLD.type_infor == escreenFinished)
    {
      dataPostGoogleSheet["type_Upload"] = "Auto";
    }
    else
    {
      dataPostGoogleSheet["type_Upload"] = "N/A";
    }

    /* Machine Specifications */
    JsonArray slopes_array = dataPostGoogleSheet["slopes"].to<JsonArray>();
    JsonArray origins_array = dataPostGoogleSheet["origins"].to<JsonArray>();
    JsonArray ledPower_array = dataPostGoogleSheet["LED_power"].to<JsonArray>();
    // JsonArray nameSlot_array = dataPostGoogleSheet["nameSlot"].to<JsonArray>();
    JsonArray CT_value_array = dataPostGoogleSheet["CT_value"].to<JsonArray>();
    JsonArray result_array = dataPostGoogleSheet["result"].to<JsonArray>();

    /* Data Read Amplification and Result (CT_value, Result) */
    JsonArray recordOut_array = dataPostGoogleSheet["record_out"].to<JsonArray>();
    JsonArray amplification_array = dataPostGoogleSheet["amplification"].to<JsonArray>();

    for (uint8_t i = 0; i < OPTOCHANNELS; i++)
    {
      String slotName = "Slot_" + String(i + 1);
      JsonObject recordOutSlot = recordOut_array.add<JsonObject>();
      JsonObject peak_featuresObj = recordOutSlot[slotName]["peak_features"].to<JsonObject>();
      JsonObject outcomeObj = recordOutSlot[slotName]["outcome"].to<JsonObject>();
      outcome[i].transition_time.x = rounded((float)outcome[i].transition_time.x);
      outcome[i].transition_time.y = rounded((float)outcome[i].transition_time.y);
      outcome[i].plateau_point.x = rounded((float)outcome[i].plateau_point.x);
      outcome[i].plateau_point.y = rounded((float)outcome[i].plateau_point.y);
      outcome[i].increase = rounded((float)outcome[i].increase);
      outcomeObj["transition_time"] = outcome[i].transition_time.toJSON();
      outcomeObj["plateau_point"] = outcome[i].plateau_point.toJSON();
      outcomeObj["increase"] = outcome[i].increase;

      peak_features[i].main_peak.x = rounded((float)peak_features[i].main_peak.x);
      peak_features[i].main_peak.y = rounded((float)peak_features[i].main_peak.y);
      peak_features[i].right_arm.x = rounded((float)peak_features[i].right_arm.x);
      peak_features[i].right_arm.y = rounded((float)peak_features[i].right_arm.y);
      peak_features[i].left_arm.x = rounded((float)peak_features[i].left_arm.x);
      peak_features[i].left_arm.y = rounded((float)peak_features[i].left_arm.y);
      peak_featuresObj["main_peak"] = peak_features[i].main_peak.toJSON();
      peak_featuresObj["right_arm"] = peak_features[i].right_arm.toJSON();
      peak_featuresObj["left_arm"] = peak_features[i].left_arm.toJSON();
    }

    for (int i = 0; i < OPTOCHANNELS; i++)
    {
      char resultConfig[30] = {0};
      /* Check Sensor Errors */
      // Serial.printf("[up] slot %d name=%s result=%c CT=%.1f\n", i + 1, (slotNames[i].length() == 0) ? "N/A" : slotNames[i].c_str(), result[i], CT_value[i]);
      // Serial.printf("[up] length of slot name=%d\n", slotNames[i].length());
      // Serial.printf("[up] the first char of slot name=%c\n", slotNames[i].charAt(0));
      if (error.searchError(errorLightSensor, errorNoData, eSensor1stReading, i) != 255 ||
          error.searchError(errorLightSensor, errorWrongData, eSensor1stReading, i) != 255 ||
          error.searchError(errorLightSensor, errorTooDark, eSensor1stReading, i) != 255 ||
          error.searchError(errorLightSensor, errorTooBright, eSensor1stReading, i) != 255)
      {
        // snprintf, not sprintf: resultConfig is 20 bytes and "%04.01f" of a large or garbage
        // CT_value (a slot with no usable curve) prints far more than that - straight over the
        // rest of this stack frame. The bound truncates instead of corrupting.
        // 'F' (v2.4.3AT, shape-flagged) joins P and S: it has a CT, and /E is an OPTICS fault on
        // the slot rather than a verdict on the amplification, so the CT still belongs here.
        if (result[i] == 'P' || result[i] == 'S' || result[i] == 'F')
        {
          snprintf(resultConfig, sizeof(resultConfig), "%s | %2.0f | /E", (slotNames[i].length() == 0) ? "N/A" : slotNames[i].c_str(), CT_value[i]);
        }
        else
        {
          snprintf(resultConfig, sizeof(resultConfig), "%s | - | /E", (slotNames[i].length() == 0) ? "N/A" : slotNames[i].c_str());
        }
      }
      else if (result[i] == 'E')
      {
        snprintf(resultConfig, sizeof(resultConfig), "%s | !  | %c", (slotNames[i].length() == 0) ? "N/A" : slotNames[i].c_str(), result[i]);
      }
      else
      {
        snprintf(resultConfig, sizeof(resultConfig), "%s | %04.01f | %c", (slotNames[i].length() == 0) ? "N/A" : slotNames[i].c_str(), CT_value[i], result[i]);
      }

      slopes_array.add(_ForteSetting.parameter.slopes[i]);
      origins_array.add(_ForteSetting.parameter.origins[i]);
      ledPower_array.add(_ForteSetting.parameter.led_power[i]);
      CT_value_array.add(rounded(CT_value[i]));
      result_array.add(resultConfig);
    }

    for (int i = 0; i < OPTOCHANNELS; i++)
    {
      String data_raw = "";
      for (int j = 0; j < loops; j++)
      {
        data_raw += String(_sensor6035.sensor67Value[i][j]) + ",";
      }
      amplification_array.add(data_raw);
    }

    serializeJson(dataPostGoogleSheet, jsonPost);
  } // <- JsonDocument destructed here, ~25-40KB returned to heap

  uint16_t tmpHttpCode = postJsonToAllTargets(jsonPost, "data");

  dashboardResume(); // bring the dashboard back now that TLS is done
  return tmpHttpCode;
  // return 200;
}

// postData_Chart() / getData_toChart() were removed: the old sync WebServer chart is
// replaced by the AsyncWebServer live dashboard (see src/webDashboard.cpp).