#include "Bluetooth.h"
#include "sensor6035.h"
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp32-hal.h>
#include "index.h"
#include "errorCheck.h"
#include "webDashboard.h"

BluetoothSerial SerialBT;
volatile bool gBtReleased = false; // see releaseBluetoothStack() / define.h
String ssid = "";
String password = "";
uint64_t epsid = ESP.getEfuseMac();
String id(String(epsid).c_str());
String id_device = "RAPIDPlus";

const char *serverName = "https://script.google.com/macros/s/AKfycbw2VXXLX6fUMgmyRrSgNgEi3b4gSyE2bdctQe_DNOnlZ58EfPclQrXrlMenH0y7SH5X/exec";
// const char *serverName2 = "https://api.fortebio.tech/api/v1/results/ingest";
const char *serverName2 = "https://fbt.basa-luma.ts.net/ingest";

const char *server_engineerToken = "***REMOVED***";

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
void paraDisplay(parastructure para)
{
  info_displayf("Length: %d\n", para.length);

  DynamicJsonDocument paradata(3000); // support maximum 3K

  paradata["para version"] = para.para_version;
  paradata["PCB version"] = para.PCB_version;
  JsonObject calibration = paradata.createNestedObject("opto calibration");
  JsonArray slopes = calibration.createNestedArray("slopes");
  JsonArray origins = calibration.createNestedArray("origins");
  JsonArray ledPower = paradata.createNestedArray("LED power");
  for (int i = 0; i < OPTOCHANNELS; i++)
  {
    slopes.add(para.slopes[i]);
    origins.add(para.origins[i]);
    ledPower.add(para.led_power[i]);
  }

  JsonObject opto_parameter = paradata.createNestedObject("parameters");
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
  paradata["device ID"] = para.device_id;
  paradata["lysis duration"] = para.lysisDuration;
  paradata["opto preheat time"] = para.optopreheatduration;
  paradata["LED Duration"] = para.LEDDuration;
  paradata["time per loop"] = para.timePerLoop;
  paradata["amplification time"] = para.amplification_time;
  paradata["lysis temperature"] = para.lysisTemp;
  paradata["amplification temperature"] = para.amplifTemp;

  JsonArray bottomTemperatureSensorSq = paradata.createNestedArray("bottom temperature sensor seq");
  for (int i = 0; i < 3; i++)
  {
    bottomTemperatureSensorSq.add(para.bottomTemperatureSensorSq[i]);
  }

  JsonArray topTemperatureSensorSq = paradata.createNestedArray("top temperature sensor seq");
  for (int i = 0; i < 3; i++)
  {
    topTemperatureSensorSq.add(para.topTemperatureSensorSq[i]);
  }

  JsonArray pid1 = paradata.createNestedArray("PID parameter");
  JsonArray pid2 = paradata.createNestedArray("PID2 parameter");
  JsonArray pid3 = paradata.createNestedArray("PID3 parameter");
  JsonArray bottomOverheat = paradata.createNestedArray("Bottom overheat value");
  JsonArray topOverheat = paradata.createNestedArray("Top overheat value");
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

  JsonArray temperatureOffset = paradata.createNestedArray("temperature value calibration");
  for (int i = 0; i < 6; i++)
  {
    temperatureOffset.add(para.temperatureOffset[i]);
  }

  paradata["buzzer"] = para.buzzerOn ? "On" : "Off";
  paradata["kitId"] = para.kitId;

  // Output metadata
  String output;
  serializeJsonPretty(paradata, output);
  info_displayln(output + "@");
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
 * Description: Persists the WiFi SSID, password and device ID to EEPROM and
 *  clears the ADDR_CHECK_ID_DEVICE flag, then commits and ends EEPROM.
 * pramameter: none
 *  return: none
 */
void saveSettingDevice()
{
  EEPROM.begin(_EEPROM_SIZE);
  EEPROM.writeString(ADDR_SSID, ssid);
  EEPROM.writeString(ADDR_PASSWORD, password);
  EEPROM.writeString(ADDR_ID_DEVICE_BASE, id_device);
  EEPROM.writeBool(ADDR_CHECK_ID_DEVICE, false);
  EEPROM.commit();
  EEPROM.end();
}

/***********************************************************************
 * Function: loadSettingDevice()
 * Description: Loads the WiFi SSID, password and device ID from EEPROM into
 *  the global ssid, password and id_device variables.
 * pramameter: none
 *  return: none
 */
void loadSettingDevice()
{
  EEPROM.begin(_EEPROM_SIZE);
  ssid = EEPROM.readString(ADDR_SSID);
  password = EEPROM.readString(ADDR_PASSWORD);
  id_device = EEPROM.readString(ADDR_ID_DEVICE_BASE);
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
 * Three flows tear BT down (auto upload in screen_Result, manual upload, and
 * WiFi setup in Wifi_Connect) and none is guaranteed to end in a restart, so
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

/**
 * @brief Connect to WiFi using WiFiManager
 *
 */
/***********************************************************************
 * Function: Wifi_Connect()
 * Description: Releases the Bluetooth stack, disables the Core 0 watchdog,
 *  then launches WiFiManager's captive-portal AP (named from id_device) to
 *  let the user enter WiFi credentials and a device ID. On connect, saves
 *  SSID/password/id_device to EEPROM; on failure, restarts the device.
 * pramameter: none
 *  return: none
 */
void Wifi_Connect()
{
  // Hard-release Bluetooth Classic stack BEFORE WiFiManager starts. SerialBT.end()
  // alone leaves controller + bluedroid (~60KB) resident; WiFiManager's AP + DNS +
  // captive-portal HTTP server can OOM during the phone's first request and reset
  // the device. Idempotent: a prior auto/manual upload may already have released
  // BT this power cycle, so this must not double-free or re-touch SerialBT.
  releaseBluetoothStack();
  Serial.printf("Before WiFiManager: free=%u, largest=%u\n",
                ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  // WiFiManager.autoConnect() blocks DisplayTask (Core 0) inside an internal
  // loop that doesn't yield enough to IDLE-0 → Task Watchdog fires (~5s default)
  // while user is on the captive portal. setting_Wifi() always ends with
  // esp_restart(), so we don't need to re-enable.
  disableCore0WDT();

  WiFiManager wifiManager;
  WiFiManagerParameter custom_id_device("id_device", "Enter ID Device", "RPL", 40);

  const char *menu[] = {"wifi", "update", "sep", "exit"};

  if (WiFi.status() == WL_CONNECTED)
  {
    WiFi.disconnect(true);
    delay(500);
  }
  dashboardEnd(); // stop the AsyncWebServer dashboard before the WiFiManager portal

  wifiManager.resetSettings(); // Xóa thông tin kết nối cũ
  wifiManager.setDebugOutput(true);
  wifiManager.setMenu(menu, 4);
  wifiManager.addParameter(&custom_id_device);
  wifiManager.setTitle("Fortebiotech RAPID Setup");

  String apName = "";
  char *tmp = "";

  EEPROM.begin(_EEPROM_SIZE);
  if (!EEPROM.readBool(ADDR_CHECK_ID_DEVICE) ||
      (strncmp(id_device.c_str(), "RPL", 3) == 0))
  {
    apName = "FBT " + id_device;
  }
  else
  {
    apName = "FBT RAPIDPlus";
  }
  EEPROM.end();

  if (!wifiManager.autoConnect(apName.c_str()))
  {
    delay(3000);
    ESP.restart();
  }

  ssid = WiFi.SSID();
  password = WiFi.psk();
  id_device = custom_id_device.getValue();
  saveSettingDevice();
}

/***********************************************************************
 * Function: getDataAmplificationEEPROM()
 * Description: Reads the stored amplification record array (10 x 130 Words)
 *  from EEPROM at RECORDPOS and copies it into _sensor6035.sensor67Value.
 * pramameter: none
 *  return: none
 */
void getDataAmplificationEEPROM(void)
{

  EEPROM.begin(_EEPROM_SIZE);
  Word tmp[10 * 130] = {0};
  EEPROM.get(RECORDPOS, tmp);
  memcpy(_sensor6035.sensor67Value, tmp, sizeof(tmp));
  EEPROM.end();
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

  // Free the dashboard's network heap (close SSE + stop AsyncWebServer) so mbedTLS
  // can allocate its contiguous handshake buffers. Otherwise: -32512 (SSL memory
  // allocation failed). dashboardResume() before every return below.
  dashboardSuspend();

  // Precondition for the TLS handshake below: the Bluetooth Classic stack must be
  // fully released so mbedTLS can allocate its ~40KB contiguous handshake buffers.
  // Idempotent (gBtReleased guard) — safe even if the caller already released BT.
  releaseBluetoothStack();

  struct DiagnosticOutcome outcome[10];
  struct FeatureDetection peak_features[10];

  /* Calculate CT_value and result */
  bool flag = _sensor6035.bResultPutToGoogleSheet(CT_value, result, outcome, peak_features);

  // Build JSON inside a nested scope so the JsonDocument is destructed
  // (and its ~25-40KB internal pool freed) BEFORE we open the TLS socket.
  // mbedTLS needs a big contiguous free block; building the doc and the
  // serialized String at the same time as the TLS handshake causes
  // X509 alloc failures (-10368) on a fragmented heap.
  String jsonPost;
  {
    JsonDocument dataPostGoogleSheet;

    dataPostGoogleSheet["method"] = "append";
    dataPostGoogleSheet["id_device"] = id_device;
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
    JsonArray slopes_array = dataPostGoogleSheet.createNestedArray("slopes");
    JsonArray origins_array = dataPostGoogleSheet.createNestedArray("origins");
    JsonArray ledPower_array = dataPostGoogleSheet.createNestedArray("LED_power");
    JsonArray CT_value_array = dataPostGoogleSheet.createNestedArray("CT_value");
    JsonArray result_array = dataPostGoogleSheet.createNestedArray("result");

    /* Data Read Amplification and Result (CT_value, Result) */
    JsonArray recordOut_array = dataPostGoogleSheet.createNestedArray("record_out");
    JsonArray amplification_array = dataPostGoogleSheet.createNestedArray("amplification");

    for (uint8_t i = 0; i < OPTOCHANNELS; i++)
    {
      String slotName = "Slot_" + String(i + 1);
      JsonObject recordOutSlot = recordOut_array.createNestedObject();
      JsonObject peak_featuresObj = recordOutSlot[slotName].createNestedObject("peak_features");
      JsonObject outcomeObj = recordOutSlot[slotName].createNestedObject("outcome");
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
      char resultConfig[15] = {0};
      /* Check Sensor Errors */
      if (error.searchError(errorLightSensor, errorNoData, eSensor1stReading, i) != 255 ||
          error.searchError(errorLightSensor, errorWrongData, eSensor1stReading, i) != 255 ||
          error.searchError(errorLightSensor, errorTooDark, eSensor1stReading, i) != 255 ||
          error.searchError(errorLightSensor, errorTooBright, eSensor1stReading, i) != 255)
      {
        if (result[i] == 'P' || result[i] == 'S')
        {
          sprintf(resultConfig, "%2.0f | /E", CT_value[i], result[i]);
        }
        else
        {
          sprintf(resultConfig, "- | /E");
        }
      }
      else if (result[i] == 'E')
      {
        sprintf(resultConfig, "!  | %c", result[i]);
      }
      else
      {
        sprintf(resultConfig, "%04.01f | %c", CT_value[i], result[i]);
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

  // Now open TLS with the maximum free heap available.
  // setInsecure() skips cert chain validation -> smaller mbedTLS allocation.
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(60); // socket-level timeout in seconds (Arduino-ESP32 WiFiClient API)
  client.setHandshakeTimeout(30);

  HTTPClient http;
  // DO NOT follow redirects: GAS /exec returns 302 -> script.googleusercontent.com.
  // HTTPClient re-POSTs the body to the redirect URL, but that host rejects POST
  // (returns Google's generic "400 Bad Request" HTML page).
  // We don't need the redirect target's body anyway — GAS has already processed
  // the POST data by the time it issues the 302, so 302 == success for us.
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  http.setReuse(false);
  http.useHTTP10(true);
  if (!http.begin(client, serverName))
  {
    Serial.println("http.begin() failed");
    dashboardResume();
    return 0;
  }
  // GAS /exec only emits the 302 AFTER doPost() finishes appending to the sheet,
  // which currently takes ~35-40s (Data sheet has grown large). The old 30s cut us
  // off mid-execution -> code=-11 (read Timeout) even though the write was fine.
  // 60s leaves margin above the observed GAS latency. (Root fix: speed up doPost.)
  http.setTimeout(60000);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Connection", "close");

  uint32_t t0 = millis();
  int httpResponseCode = http.POST(jsonPost);
  uint16_t tmpHttpCode = httpResponseCode;
  uint32_t dt = millis() - t0;

  // 2xx = direct success; 302 from GAS = script accepted and processed the data.
  bool ok = (httpResponseCode >= 200 && httpResponseCode < 300) ||
            (httpResponseCode == HTTP_CODE_FOUND); // 302
  if (ok)
  {
    Serial.printf("POST OK in %u ms, code=%d\n", dt, httpResponseCode);
  }
  else if (httpResponseCode > 0)
  {
    Serial.printf("POST HTTP error in %u ms, code=%d, response=%s\n",
                  dt, httpResponseCode, http.getString().c_str());
  }
  else
  {
    Serial.printf("POST FAIL in %u ms, code=%d (%s)\n",
                  dt, httpResponseCode, http.errorToString(httpResponseCode).c_str());
  }
  delay(100); // give the TLS handshake a moment to complete before POSTing
  http.end();
  delay(100); // give the TLS handshake a moment to complete before POSTing

  // Now POST to the ForteBio ingest API. This is a separate endpoint from GAS /exec
  // and is used for the cloud dashboard. It expects the same JSON payload, but
  // requires an API key in the Authorization header.
  if (!http.begin(client, serverName2))
  {
    Serial.println("client.connect() failed");
    http.end();
    dashboardResume();
    return 0;
  }
  http.setTimeout(60000);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Connection", "close");
  // ingest API rejects unauthenticated POSTs with 401 -> send the Bearer token.
  http.addHeader("Authorization", String("Bearer ") + server_engineerToken);

  Serial.printf("server request: %s\n", jsonPost.c_str());

  t0 = millis();
  httpResponseCode = http.POST(jsonPost);

  dt = millis() - t0;

  // Đọc body server trả về SAU khi POST (getString() phải gọi sau POST mới có nội dung).
  String server_feedback = http.getString();
  Serial.printf("server feedback: %s\n", server_feedback.c_str());

  // 2xx = direct success; 302 from GAS = script accepted and processed the data.
  ok = (httpResponseCode >= 200 && httpResponseCode < 300) ||
       (httpResponseCode == HTTP_CODE_FOUND); // 302
  if (ok)
  {
    Serial.printf("POST OK in %u ms, code=%d\n", dt, httpResponseCode);
  }
  else if (httpResponseCode > 0)
  {
    Serial.printf("POST HTTP error in %u ms, code=%d, response=%s\n",
                  dt, httpResponseCode, server_feedback.c_str());
  }
  else
  {
    Serial.printf("POST FAIL in %u ms, code=%d (%s)\n",
                  dt, httpResponseCode, http.errorToString(httpResponseCode).c_str());
  }

  delay(100); // give the TLS handshake a moment to complete before POSTing
  http.end();
  dashboardResume(); // bring the dashboard back now that TLS is done
  // return tmpHttpCode;
  return 200;
}

/***********************************************************************
 * Function: getResult_toChart()
 * Description: Maps a single-character result code to a human-readable
 *  string: 'N'->"Negative", 'P'->"Positive", 'S'->"Slide Positive",
 *  'E'->"E".
 * pramameter: tmp - the result character code
 *  return: String - the readable result label for the chart
 */
String getResult_toChart(char tmp)
{
  if (tmp == 'N')
  {
    return "Negative";
  }
  else if (tmp == 'P')
  {
    return "Positive";
  }
  else if (tmp == 'S')
  {
    return "Slide Positive";
  }
  else if (tmp == 'E')
  {
    return "E";
  }
}

/***********************************************************************
 * Function: getCT_toChart()
 * Description: Formats a CT value for the chart: returns "N/A" when the
 *  result is Negative ('N'), otherwise returns the CT value as a string.
 * pramameter: tmp - the CT value
 * pramameter: result - the result character code
 *  return: String - "N/A" for negative results, else the CT value
 */
String getCT_toChart(float tmp, char result)
{
  if (result == 'N')
  {
    return "N/A";
  }
  else
  {
    return String(tmp);
  }
}

/***********************************************************************
 * Function: getData_toChart()
 * Description: Loads amplification data from EEPROM, computes per-slot CT
 *  values and results, and serializes them along with the device ID and
 *  the processed amplification curves into a JSON string for the chart web
 *  page. Frees the per-slot processed_data buffers after use.
 * pramameter: none
 *  return: String - the serialized JSON of chart data
 */
String getData_toChart(void)
{
  JsonDocument readings;
  String JsonString = "";
  float CT_value[10] = {0};
  char result[10] = {0};
  float *processed_data[10] = {NULL};

  uint8_t loops = _ForteSetting.parameter.amplification_time;

  readings["id_device"] = id_device;

  getDataAmplificationEEPROM();

  bool flag = _sensor6035.bResultPutToChart(CT_value, result, processed_data);

  for (size_t i = 0; i < OPTOCHANNELS; i++)
  {
    readings["CT_value"][i] = getCT_toChart(CT_value[i], result[i]);
    readings["result"][i] = getResult_toChart(result[i]);

    for (uint8_t j = 0; j < loops; j++)
    {
      readings[String("#") + String(i + 1)][j] = String(processed_data[i][j]);
    }

    free(processed_data[i]);
  }

  serializeJson(readings, JsonString);

  Serial.println(JsonString);

  return JsonString;
}

// postData_Chart() was removed: the old sync WebServer chart is replaced by the
// AsyncWebServer live dashboard (see src/webDashboard.cpp). getData_toChart()
// below is retained for a possible future /readings endpoint.