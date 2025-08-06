#ifndef BLUETOOTH_H
#define BLUETOOTH_H

#include "define.h"
#include "BluetoothSerial.h"
#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif

#include "NTPClient.h"
#include <WiFiManager.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include "LittleFS.h"

#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

struct parastructure; // forward declaration
extern String ssid;
extern String password;
extern String id_device;
extern String id;

extern BluetoothSerial SerialBT;

extern WebServer server;

void connectBLE();
void BLEloop();
// void saveParaToEEPROM();
void saveJsonToEEPROM(char *jsondata, uint32_t jsonlen);
void readEEPROM();
void paraDisplay(parastructure);
void loadParaFromEEPROM();
void loadJsonFromEEPROM();
// bool loadJsonFromEEPROM(char *jsondata);

void connectWIFI();
void saveSettingDevice();
void loadSettingDevice();
void Write_language_ToEEPROM();
void Read_language_fromEEPROM();
void getDataAmplificationEEPROM(void);
void getData(void);
String getData_toChart(void);
String getCT_toChart(float tmp, char c);
String getResult_toChart(char tmp);
// void sendJsonInChunks(const String& json, size_t chunkSize);
// void handleGetData(void);

/**
 * @brief Connect to WiFi using WiFiManager
 * @version 2.1
 *
 */
void Wifi_Connect(void);

/**
 * @brief googlesheet API
 * @version 2.2
 */
void postData_GoogleSheet(float CT_value[10], char result[10], uint8_t loops);

/**
 * @brief postData_Chart
 *
 */
void postData_Chart(void);

#endif