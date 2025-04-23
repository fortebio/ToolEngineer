#include "Bluetooth.h"
#include "sensor6035.h"
#include <ArduinoJson.h>

BluetoothSerial SerialBT;
String ssid = "";
String password = "";
uint64_t epsid = ESP.getEfuseMac();
String id(String(epsid).c_str());
String id_device = "";

extern int language = 0;

const char *serverName = "https://script.google.com/macros/s/AKfycbyJhA5ZXUeFduVaUpTLi5vC-5TIr9mst5pZ9A0DkDARX1xlXVv7y-1w1BN3p0oBisKYXA/exec";
const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 25200; // Múi giờ GMT+7 (Việt Nam)
const int daylightOffset_sec = 0;

void connectBLE()
{
  SerialBT.begin("RAPID PLUS -" + String(ESP.getEfuseMac())); // Bluetooth device name
  // dbg_bluetooth("The device started with name BTDetector-%s, now you can pair it with bluetooth!\n", String(ESP.getEfuseMac()).c_str());
}

void BLEloop()
{
  if (SerialBT.available())
  {
    SerialBT.printf("received data: %s\n", SerialBT.readString());
    dbg_bluetooth("receive data");
  }
  // SerialBT.write('t');
  delay(300);
  // }
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
  WiFiManagerParameter custom_id_device("id_device", "Enter ID Device", "RA", 40);

  const char *menu[] = {"wifi", "update", "sep", "exit"};

  if (WiFi.status() == WL_CONNECTED)
  {
    SerialBT.end();
    WiFi.disconnect(true);
    delay(500);
  }

  wifiManager.resetSettings(); // Xóa thông tin kết nối cũ
  wifiManager.setDebugOutput(true);
  wifiManager.setMenu(menu, 4);
  wifiManager.addParameter(&custom_id_device);
  wifiManager.setTitle("Fortebiotech Rapid Setup");

  if (!wifiManager.autoConnect("FBT_RAPID PLUS"))
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
  Word tmp[10 * 100] = {0};
  EEPROM.get(RECORDPOS, tmp);
  memcpy(_sensor6035.sensor67Value, tmp, sizeof(tmp));
  EEPROM.end();
}

void postData_GoogleSheet(void)
{
  if (WiFi.status() == WL_CONNECTED)
  {
    info_displayln("Read data Amplifications from EEPROM");

    int CT_value[10];
    char result[10];
    JsonDocument dataPostGoogleSheet;
    String jsonPost = "";
    uint8_t loops = _ForteSetting.parameter.amplification_time;

    getDataAmplificationEEPROM();

    SerialBT.end();
    HTTPClient http;
    http.begin(serverName);
    http.addHeader("Content-Type", "application/json");
    Serial.println("Truoc khi ket noi: " + String(ESP.getFreeHeap()));

    /* Calculate CT_value and result */
    bool flag = _sensor6035.bResultPutToGoogleSheet(CT_value, result);
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    String timeString = getTime();

    dataPostGoogleSheet["method"] = "append";
    dataPostGoogleSheet["id_device"] = id_device;
    dataPostGoogleSheet["version"] = FirmwareVer;
    dataPostGoogleSheet["time"] = timeString;

    // dataPostGoogleSheet["amplification_time"] = _ForteSetting.parameter.amplification_time;

    /* Machine Specifications */
    JsonArray slopes_array = dataPostGoogleSheet.createNestedArray("slopes");
    JsonArray origins_array = dataPostGoogleSheet.createNestedArray("origins");
    JsonArray ledPower_array = dataPostGoogleSheet.createNestedArray("LED_power");
    JsonArray CT_value_array = dataPostGoogleSheet.createNestedArray("CT_value");
    JsonArray result_array = dataPostGoogleSheet.createNestedArray("result");

    /* Data Read Amplification and Result (CT_value, Result) */
    JsonArray amplification_array = dataPostGoogleSheet.createNestedArray("amplification");
    JsonObject SlotObj = amplification_array.createNestedObject();

    for (int i = 0; i < OPTOCHANNELS; i++)
    {
      // String result = String(CT_value[i]) + " | " + String(result[i]);
      slopes_array.add(_ForteSetting.parameter.slopes[i]);
      origins_array.add(_ForteSetting.parameter.origins[i]);
      ledPower_array.add(_ForteSetting.parameter.led_power[i]);
      CT_value_array.add(CT_value[i]);
      result_array.add(deseaseConclusion(result[i]));
    }

    String const amplification_channel[10] = {"Slot 1",
                                              "Slot 2",
                                              "Slot 3",
                                              "Slot 4",
                                              "Slot 5",
                                              "Slot 6",
                                              "Slot 7",
                                              "Slot 8",
                                              "Slot 9",
                                              "Slot 10"};
    for (int i = 0; i < OPTOCHANNELS; i++)
    {
      JsonArray amplification_channel_array = SlotObj.createNestedArray(amplification_channel[i]);
      for (int j = 0; j < loops; j++)
      {
        amplification_channel_array.add(_sensor6035.sensor67Value[i][j]);
      }
    }

    serializeJson(dataPostGoogleSheet, jsonPost);
    Serial.println("Post data: " + jsonPost);
    // Kết nối HTTPS và gửi dữ liệu
    int httpResponseCode = http.POST(jsonPost);
    Serial.println("Sau khi ket noi: " + String(ESP.getFreeHeap()));
    _displayCLD.changeScreen = false;
    http.end();
    if (httpResponseCode > 0)
    {
      String response = http.getString();
      Serial.println("Response code: " + String(httpResponseCode));
      Serial.println("Response: " + response);
      Serial.println("Data posted successfully!");
    }
    else
    {
      Serial.println("Error on sending POST: " + String(httpResponseCode));
    }
  }
  else
  {
    Serial.println("WiFi disconnected!");
  }
}