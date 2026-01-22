#include "Bluetooth.h"
#include "sensor6035.h"
#include <ArduinoJson.h>
#include "index.h"

BluetoothSerial SerialBT;
String ssid = "";
String password = "";
uint64_t epsid = ESP.getEfuseMac();
String id(String(epsid).c_str());
// String id_device = "";
String id_device = "RAPIDPlus";

extern int language = 0;

const char *serverName = "https://script.google.com/macros/s/AKfycbw2VXXLX6fUMgmyRrSgNgEi3b4gSyE2bdctQe_DNOnlZ58EfPclQrXrlMenH0y7SH5X/exec";

const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 25200; // Múi giờ GMT+7 (Việt Nam)
const int daylightOffset_sec = 0;

void connectBLE()
{
  SerialBT.begin("RAPID PLUS -" + String(ESP.getEfuseMac())); // Bluetooth device name
  // bool status_BT = false;
  // EEPROM.begin(_EEPROM_SIZE);
  // EEPROM.get(ADDR_CHECK_BT, status_BT);
  // if (status_BT == false)
  // {
  //   SerialBT.begin("RAPID PLUS -" + String(ESP.getEfuseMac())); // Bluetooth device name
  //   status_BT = true;
  //   EEPROM.put(ADDR_CHECK_BT, status_BT);
  // }
  // else
  // {
  //   SerialBT.end();
  //   status_BT = false;
  //   EEPROM.put(ADDR_CHECK_BT, status_BT);
  // }
  // EEPROM.end();
}

void BLEloop()
{
  if (SerialBT.available())
  {
    SerialBT.printf("received data: %s\n", SerialBT.readString());
    dbg_bluetooth("receive data");
  }
  delay(300);
}

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

void paraDisplay(parastructure para)
{
  info_displayf("Length: %d\n", para.length);

  DynamicJsonDocument paradata(3000); // support maximum 3K

  paradata["para version"] = para.para_version;
  // info_displayf("para version: %s\n", para.para_version);
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
  JsonArray bottomOverheat = paradata.createNestedArray("Bottom overheat value");
  JsonArray topOverheat = paradata.createNestedArray("Top overheat value");
  for (int i = 0; i < 3; i++)
  {
    pid1.add(para.kpid[i]);
    pid2.add(para.kpid2[i]);
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

  JsonArray hotlidPWM = paradata.createNestedArray("top heater PWM");
  for (int i = 0; i < 2; i++)
  {
    JsonArray row = hotlidPWM.createNestedArray();
    for (int j = 0; j < 2; j++)
    {
      row.add(para.hotlidPWM[i][j]);
    }
  }
  paradata["buzzer"] = para.buzzerOn ? "On" : "Off";
  paradata["kitId"] = para.kitId;

  JsonArray empty = paradata.createNestedArray("empty");
  for (uint8_t i = 0; i < 5; i++)
  {
    empty.add(para.empty[i]);
  }

  // Output metadata
  String output;
  serializeJsonPretty(paradata, output);
  info_displayln(output + "@");
}

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

void loadSettingDevice()
{
  EEPROM.begin(_EEPROM_SIZE);
  ssid = EEPROM.readString(ADDR_SSID);
  password = EEPROM.readString(ADDR_PASSWORD);
  id_device = EEPROM.readString(ADDR_ID_DEVICE_BASE);
  EEPROM.end();
}

void Write_language_ToEEPROM()
{
  EEPROM.begin(_EEPROM_SIZE);
  EEPROM.write(200, language);
  delay(50);
  EEPROM.commit();
}

void Read_language_fromEEPROM()
{
  EEPROM.begin(_EEPROM_SIZE);
  language = EEPROM.read(200);
  delay(50);
  EEPROM.end();
}

/**
 * @brief Connect to WiFi using WiFiManager
 *
 */
void Wifi_Connect()
{
  WiFiManager wifiManager;
  WiFiManagerParameter custom_id_device("id_device", "Enter ID Device", "RPL", 40);

  const char *menu[] = {"wifi", "update", "sep", "exit"};

  if (WiFi.status() == WL_CONNECTED)
  {
    SerialBT.end();
    WiFi.disconnect(true);
    delay(500);
  }
  server.stop();

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

String getTime()
{
  struct tm timeinfo;
  char timeString[50];

  // Get local time
  if (!getLocalTime(&timeinfo))
  {
    Serial.println("Failed to obtain time");
    return String("N/A");
  }
  strftime(timeString, sizeof(timeString), "%d-%m-%Y %H:%M:%S", &timeinfo);
  return String(timeString);
}

void getDataAmplificationEEPROM(void)
{

  EEPROM.begin(_EEPROM_SIZE);
  Word tmp[10 * 130] = {0};
  EEPROM.get(RECORDPOS, tmp);
  memcpy(_sensor6035.sensor67Value, tmp, sizeof(tmp));
  EEPROM.end();
}

float rounded(float value)
{
  return round(value * 10.0) / 10.0f; // Round to 1 decimal place
}

void postData_GoogleSheet(float CT_value[10], char result[10], uint8_t loops)
{
  if (WiFi.status() == WL_CONNECTED)
  {
    struct DiagnosticOutcome outcome[10];
    struct FeatureDetection peak_features[10];
    JsonDocument dataPostGoogleSheet;
    String jsonPost = "";
    String timeString = getTime();

    HTTPClient http;
    http.begin(serverName);
    http.addHeader("Content-Type", "application/json");

    /* Calculate CT_value and result */
    bool flag = _sensor6035.bResultPutToGoogleSheet(CT_value, result, outcome, peak_features);
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

    dataPostGoogleSheet["method"] = "append";
    dataPostGoogleSheet["id_device"] = id_device;
    dataPostGoogleSheet["version"] = FirmwareVer;
    dataPostGoogleSheet["time"] = timeString;
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
      // JsonObject outcomeObj = outcomeSlot[slotName].createNestedObject();
      // JsonObject outcomeObj[] = outcomeSlot.create;
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

    for (uint8_t i = 0; i < OPTOCHANNELS; i++)
    {
    }

    for (int i = 0; i < OPTOCHANNELS; i++)
    {
      char resultConfig[15] = {0};
      if (result[i] == 'E')
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
      // JsonArray amplification_channel_array = SlotObj.createNestedArray(amplification_channel[i]);
      String data_raw = "";
      for (int j = 0; j < loops; j++)
      {
        data_raw += String(_sensor6035.sensor67Value[i][j]) + ",";
      }
      amplification_array.add(data_raw);
    }

    serializeJson(dataPostGoogleSheet, jsonPost);
    // Serial.println("Post data: " + jsonPost);
    //// Kết nối HTTPS và gửi dữ liệu
    int httpResponseCode = http.POST(jsonPost);
    http.end();
    // if (httpResponseCode > 0)
    // {
    //   String response = http.getString();
    //   Serial.println("Response code: " + String(httpResponseCode));
    //   Serial.println("Response: " + response);
    //   Serial.println("Data posted successfully!");
    // }
    // else
    // {
    //   Serial.println("Error on sending POST: " + String(httpResponseCode));
    // }
  }
  else if (WiFi.status() == WL_DISCONNECTED)
  {
  }
}

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

void postData_Chart(void)
{
  if (WiFi.status() == WL_CONNECTED)
  {
    /*turn off BT */
    SerialBT.end();

    server.on("/", HTTP_GET, []()
              { server.send(200, "text/html", index_html); });

    server.on("/getdata", HTTP_GET, []()
              {
      String json = getData_toChart();
      server.send(200, "application/json", json); });

    server.begin();
  }
  else
  {
    Serial.println("Wifi disconected!");
  }
}