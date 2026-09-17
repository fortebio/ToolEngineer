#include "bluetooth.h"

BluetoothSerial SerialBT;
WiFiManager wifiManager;

String ssid = "";
String password = "";
String id_BLE = "";
String id_device = "";

uint64_t espid = ESP.getEfuseMac();

const char *serverName = "https://script.google.com/macros/s/AKfycbwG0mHzHB26xKl4Fc14cJ5C9FzgI_sTBLgLg5fKvUtowtH-KIGNqJd7XFDEw7UEOJqwyw/exec";

const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 25200; // Múi giờ GMT+7 (Việt Nam)
const int daylightOffset_sec = 0;

String set_nameBLE()
{
  char buffer[30];
  sprintf(buffer, "%lld\0", espid);
  SerialBT.begin("ESP_READER-" + String(buffer)); // Bluetooth device name

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
  //EEPROM.writeString(ADDR_ID_BLE, id_BLE);
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
  //id_BLE = EEPROM.readString(ADDR_ID_BLE);
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

void settingBLE_ThresholdPositive(void)
{
  id_BLE = set_nameBLE();

  EEPROM.begin(_EEPROM_SIZE);

  for (uint8_t addr_threshold = PC; addr_threshold < SICK_NUMBER; addr_threshold++)
  {
    getName_ThresholdPositive((sick_type)addr_threshold);
    SerialBT.printf("%s: ", getName_ThresholdPositive((sick_type)addr_threshold));

    _sensor.valueThreshold[addr_threshold] = (uint32_t)round(EnterCalib());
    SerialBT.println(_sensor.valueThreshold[addr_threshold]);
    EEPROM.put(ADDR_THRESHOLD_POSITIVE(addr_threshold), _sensor.valueThreshold[addr_threshold]);
    delay(1000);
  }
  EEPROM.commit();
  EEPROM.end();
  return;
}

uint32_t getData_ThresholdPositive(sick_type sick)
{
}

String getName_ThresholdPositive(sick_type sick)
{
  String nameSick = "";
  switch (sick)
  {
  case PC:
  {
    // SerialBT.print("PC: ");
    nameSick = "PC";
    break;
  }
  case TPD:
  {
    // SerialBT.print("TPD: ");
    nameSick = "TPD";
    break;
  }
  case EHP:
  {
    // SerialBT.print("EHP: ");
    nameSick = "EHP";
    break;
  }
  case EMS:
  {
    // SerialBT.print("EMS: ");
    nameSick = "EMS";
    break;
  }
  case WSSV:
  {
    // SerialBT.print("WSSV: ");
    nameSick = "WSSV";
    break;
  }
  default:
    break;
  }
  return nameSick;
}

void postData_GoogleSheet(sick_type sick, uint32_t val_sensor1, uint32_t val_sensor2, uint32_t val_sensor3, uint32_t val_sensor4, String id)
{
  if (WiFi.status() == WL_CONNECTED)
  {
    HTTPClient http;
    http.begin(serverName);
    http.setConnectTimeout(5000);
    http.addHeader("Content-Type", "application/json");

    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    String timeString = getTime();

    String jsonData = "{" 
                        "\"method\":\"append\","
                        "\"sick\":\"" + getName_ThresholdPositive(sick) + "\","
                        "\"value_sensor1\":" + String(val_sensor1) + ","
                        "\"value_sensor2\":" + String(val_sensor2) + ","
                        "\"value_sensor3\":" + String(val_sensor3) + ","
                        "\"value_sensor4\":" + String(val_sensor4) + ","
                        "\"data_IDdevice\":\"" + id + "\","
                        "\"date\":\"" + timeString + "\","
                        "\"version\":\"" + FirmwareVer + "\""
                      "}";
    //Serial.println("Data: " + jsonData);
    int httpResponseCode = http.POST(jsonData);

    /*
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
    */
    http.end();
  }
  else
  {
    Serial.println("WiFi disconnected!");
  }
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
  WiFiManagerParameter Threshold_PC("PC", "Enter PC Threshold", "600", 5);
  WiFiManagerParameter Threshold_EHP("EHP", "Enter EHP Threshold", "600", 5);
  WiFiManagerParameter Threshold_EMS("EMS", "Enter EMS Theshold", "600", 5);
  WiFiManagerParameter Threshold_WSSV("WSSV", "Enter WSSV Threshold", "600", 5);
  WiFiManagerParameter Threshold_TPD("TPD", "Enter TPD Threshold", "600", 5);
  const char *menuWifi[] = {"param", "sep", "exit"};
  if (WiFi.status() == WL_CONNECTED)
  {
    WiFi.disconnect(true);
    delay(500);
  }

  wifiManager.resetSettings(); // Xóa thông tin kết nối cũ
  wifiManager.setDebugOutput(false);
  wifiManager.setSaveParamsCallback(saveConfigCallback);
  wifiManager.setMenu(menuWifi, 3);
  wifiManager.addParameter(&Threshold_PC);
  wifiManager.addParameter(&Threshold_EHP);
  wifiManager.addParameter(&Threshold_EMS);
  wifiManager.addParameter(&Threshold_WSSV);
  wifiManager.addParameter(&Threshold_TPD);
  wifiManager.setTitle("Fortebiotech Rapid Setup");

  if (!wifiManager.autoConnect("FBT_RAPID"))
  {
    // delay(3000);
    // ESP.restart();
  }
  EEPROM.begin(_EEPROM_SIZE);
  _sensor.valueThreshold[PC] = String(Threshold_PC.getValue()).toInt();
  _sensor.valueThreshold[EHP] = String(Threshold_EHP.getValue()).toInt();
  _sensor.valueThreshold[EMS] = String(Threshold_EMS.getValue()).toInt();
  _sensor.valueThreshold[WSSV] = String(Threshold_WSSV.getValue()).toInt();
  _sensor.valueThreshold[TPD] = String(Threshold_TPD.getValue()).toInt();
  for (uint8_t i = PC; i < SICK_NUMBER; i++)
  {
    // _sensor.valueThreshold[i] = uint16_t(atoi(Threshold_PC.getValue()));
    Serial.printf("%d\r\n",_sensor.valueThreshold[i]);
    EEPROM.put(ADDR_THRESHOLD_POSITIVE(i), _sensor.valueThreshold[i]);
  }
  EEPROM.commit();
  EEPROM.end();

  delay(3000);
  ESP.restart();
}
