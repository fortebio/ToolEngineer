#include "Bluetooth.h"
#include "NTPClient.h"
#include <HTTPClient.h>

BluetoothSerial SerialBT;
WiFiManager wifiManager;

String ssid = " ";
String password = " ";
String id_BLE = "";
String id_device = " ";
bool blockSend = true;
bool flag_groupData = false;
bool flag_groupState = END_GROUP_DATA;

uint64_t espid = ESP.getEfuseMac();

const char *serverName = "https://script.google.com/macros/s/AKfycbx1ELG1Nq32vOegrqLo1ZtiVWeTF2R2uPvpWJHdv7cHZETLZXg6J2mP-nQevyEsg1keJQ/exec";

const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 25200; // Múi giờ GMT+7 (Việt Nam)
const int daylightOffset_sec = 0;

String set_nameBLE()
{
  char buffer[30];
  sprintf(buffer, "%lld\0", espid);
  SerialBT.begin("ESP_READER-" + String(buffer)); // Bluetooth device name
  // _displayLCD.display->setTextSize(1);
  // _displayLCD.display->setTextColor(WHITE);
  // _displayLCD.display->setCursor(20, 230);
  // _displayLCD.display->printf("ID: ESP_READER-");
  // _displayLCD.display->println(buffer);
  // dbg_bluetooth("The device started with name BTDetector-%s, now you can pair it with bluetooth!\n", buffer);
  while (!SerialBT.hasClient())
  {
    delay(50); // Chờ một thiết bị kết nối
    Serial.print(".");
  }
  delay(1000);
  SerialBT.println("Bluetooth Connected!");
  return buffer;
}

void connectWIFI()
{
  ssid = "";
  password = "";
  id_device = "";
  id_BLE = set_nameBLE();

  SerialBT.println("Enter Wifi ID:");
  while (ssid.isEmpty())
  {
    ssid = SerialBT.readString();
    ssid.trim();
  }
  SerialBT.println("Wifi is " + ssid);
  SerialBT.println("Enter Wifi password:");
  while (password.isEmpty())
  {
    password = SerialBT.readString();
    password.trim();
  }
  SerialBT.println("Password is " + password);

  SerialBT.println("Enter ID machine:");
  while (id_device.isEmpty())
  {
    id_device = SerialBT.readString();
    id_device.trim();
  }

  SerialBT.println("ID is " + id_device);
  SerialBT.println("Setup completed!");
  delay(3000);
  SerialBT.end();
}

void saveCredentialsToEEPROM()
{
  EEPROM.begin(_EEPROM_SIZE);
  EEPROM.writeString(ADDR_ID_DEVICE_BASE, id_device);
  EEPROM.writeString(ADDR_SSID, ssid);
  EEPROM.writeString(ADDR_PASSWORD, password);
  // EEPROM.writeString(ADDR_ID_BLE, id_BLE);
  EEPROM.commit();
  EEPROM.end();
}

void loadCredentialsFromEEPROM()
{
  // unsigned char ssidLength;
  EEPROM.begin(_EEPROM_SIZE);
  id_device = EEPROM.readString(ADDR_ID_DEVICE_BASE);
  ssid = EEPROM.readString(ADDR_SSID);
  password = EEPROM.readString(ADDR_PASSWORD);
  EEPROM.end();
}

float EnterCalib(void)
{
  String s_ValueCalib = "";

  while (s_ValueCalib.isEmpty())
  {
    if (SerialBT.available())
    {
      s_ValueCalib = SerialBT.readStringUntil('\n');
      s_ValueCalib.trim();
    }
    else
    {
      delay(100);
    }
  }
  return (float)(s_ValueCalib.toFloat());
}

String getTime()
{
  struct tm timeinfo;
  char timeStringBuff[50];

  // Get local time
  if (!getLocalTime(&timeinfo))
  {
    Serial.println("Failed to obtain time");
    return String("N/A"); // Return "N/A" if time cannot be obtained
  }

  // Format time as "DD-MM-YYYY HH:MM:SS"
  strftime(timeStringBuff, sizeof(timeStringBuff), "%d-%m-%Y %H:%M:%S", &timeinfo);

  return String(timeStringBuff);
}

void postData_GoogleSheet(uint8_t sick, uint32_t sensor_val1, uint32_t sensor_val2, uint32_t sensor_val3, String id)
{
#define TIMEOUT_POST_DATA 5000 // Thời gian timeout cho việc gửi dữ liệu
  String val = "";

  for (size_t i = 0; i < numSampling; i++)
  {
    if (_sensor.sensorError[i] == false)
    {
      val = " - Error_Sensor";
      break;
    }
    val = "";
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    HTTPClient http;
    http.begin(serverName);
    http.setConnectTimeout(TIMEOUT_POST_DATA); // Set timeout for HTTP connection
    http.addHeader("Content-Type", "application/json");

    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    String timeString = getTime();

    String jsonData = "{"
                      "\"method\":\"append\","
                      "\"sick\":\"" +
                      measure_value + " - " + Sample_measure +  val + "\","
                                                                             "\"sensor_value1\":" +
                      String(sensor_val1) + ","
                                            "\"sensor_value2\":" +
                      String(sensor_val2) + ","
                                            "\"sensor_value3\":" +
                      String(sensor_val3) + ","
                                            "\"data_IDdevice\":\"" +
                      id + "\","
                           "\"date\":\"" +
                      timeString + "\","
                                   "\"version\":\"" +
                      FirmwareVer + "\""
                                    "}";

    int httpResponseCode = http.POST(jsonData);
    http.end();
  }
  else
  {
    Serial.println("WiFi disconnected!");
  }
  blockSend = true;
}

void settingWifi(void)
{
  WiFiManagerParameter custom_id_device("id_device", "Enter ID Device", "RE", 40);

  const char *menuWifi[] = {"wifi", "sep", "exit"};

  if (WiFi.status() == WL_CONNECTED)
  {
    WiFi.disconnect(true);
    delay(500);
  }

  wifiManager.resetSettings(); // Xóa thông tin kết nối cũ
  wifiManager.setDebugOutput(false);
  wifiManager.setMenu(menuWifi, 3);
  wifiManager.addParameter(&custom_id_device);
  wifiManager.setTitle("Fortebiotech Rapid Setup");

  if (!wifiManager.autoConnect("FBT_RAPID"))
  {
    delay(3000);
    ESP.restart();
  }

  // Lưu thông tin kết nối vào biến toàn cục
  ssid = WiFi.SSID();
  password = WiFi.psk();
  id_device = custom_id_device.getValue();
}

void settingUpdate(void)
{
  const char *menuWifi[] = {"update", "sep", "exit"};
  if (WiFi.status() == WL_CONNECTED)
  {
    WiFi.disconnect(true);
    delay(500);
  }
  wifiManager.resetSettings(); // Xóa thông tin kết nối cũ
  wifiManager.setDebugOutput(false);
  wifiManager.setMenu(menuWifi, 3);
  wifiManager.setTitle("Fortebiotech Rapid Setup");

  if (!wifiManager.autoConnect("FBT_RAPID"))
  {
    delay(3000);
    ESP.restart();
  }
}
void saveConfigCallback(void)
{
  wifiManager.stopConfigPortal(); // ép captive portal đóng
}

void settingThreshold(void)
{
  WiFiManagerParameter **sample;
  sample = new WiFiManagerParameter *[animals.items.numberSick];

  String idBuf[10];
  String labelBuf[10];

  for (uint8_t i = 0; i < animals.items.numberSick; i++)
  {
    idBuf[i] = String(animals.items.listSick[i].sickName);
    labelBuf[i] = "Enter " + String(animals.items.listSick[i].sickName) + " Threshold";
    sample[i] = new WiFiManagerParameter(idBuf[i].c_str(), labelBuf[i].c_str(), "", 6);
  }

  const char *menuWifi[] = {"param", "sep", "exit"};
  if (WiFi.status() == WL_CONNECTED)
  {
    WiFi.disconnect(true);
    delay(500);
  }

  wifiManager.resetSettings(); // Xóa thông tin kết nối cũ
  wifiManager.setDebugOutput(true);
  wifiManager.setSaveParamsCallback(saveConfigCallback);
  wifiManager.setMenu(menuWifi, 3);
  for (uint8_t i = 0; i < animals.items.numberSick; i++)
  {
    wifiManager.addParameter(sample[i]);
  }

  wifiManager.setTitle("Fortebiotech Rapid Setup");

  if (!wifiManager.autoConnect("FBT_RAPID"))
  {
    delay(1000);
  }

  EEPROM.begin(_EEPROM_SIZE);
  for (uint8_t i = 0; i < animals.items.numberSick; i++)
  {
    animals.items.listSick[i].threshold = String(sample[i]->getValue()).toInt();
  }
  writeAnimalEeprom(ADDR_SAMPLE_DATA(animals_RD), animals);

  EEPROM.commit();
  EEPROM.end();

  for (uint8_t i = 0; i < animals.items.numberSick; i++)
  {
    delete sample[i];
  }
  delete[] sample;

  delay(1000);
  ESP.restart();
}

void postData_StartGroup(String id)
{
#define START_GROUP String("ST")
  if (WiFi.status() == WL_CONNECTED)
  {
    HTTPClient http;
    http.begin(serverName);
    http.addHeader("Content-Type", "application/json");

    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    String timeString = getTime();
    String jsonData = "{"
                      "\"method\":\"append\","
                      "\"sick\":\"" +
                      START_GROUP + "\","
                                    "\"sensor_value1\":" +
                      "\"-\"" + ","
                                "\"sensor_value2\":" +
                      "\"-\"" + ","
                                "\"sensor_value3\":" +
                      "\"-\"" + ","
                                "\"data_IDdevice\":\"" +
                      id + "\","
                           "\"date\":\"" +
                      timeString + "\","
                                   "\"version\":\"" +
                      FirmwareVer + "\""
                                    "}";

    Serial.println("Data: " + jsonData);
    int httpResponseCode = http.POST(jsonData);
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
    http.end();
  }
  else
  {
    Serial.println("WiFi disconnected!");
  }
}

void postData_EndGroup(String id)
{
#define END_GROUP String("EN")
  if (WiFi.status() == WL_CONNECTED)
  {
    HTTPClient http;
    http.begin(serverName);
    http.addHeader("Content-Type", "application/json");

    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    String timeString = getTime();
    String jsonData = "{"
                      "\"method\":\"append\","
                      "\"sick\":\"" +
                      END_GROUP + "\","
                                  "\"sensor_value1\":" +
                      "\"-\"" + ","
                                "\"sensor_value2\":" +
                      "\"-\"" + ","
                                "\"sensor_value3\":" +
                      "\"-\"" + ","
                                "\"data_IDdevice\":\"" +
                      id + "\","
                           "\"date\":\"" +
                      timeString + "\","
                                   "\"version\":\"" +
                      FirmwareVer + "\""
                                    "}";

    Serial.println("Data: " + jsonData);
    int httpResponseCode = http.POST(jsonData);
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
    http.end();
  }
  else
  {
    Serial.println("WiFi disconnected!");
  }
}