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
extern const char *serverName;

extern volatile bool gBtReleased;
void releaseBluetoothStack();

void connectBLE();
// void saveParaToEEPROM();
void readEEPROM();
void paraDisplay(parastructure);
void loadParaFromEEPROM();

void connectWIFI();
void saveSettingDevice();
void loadSettingDevice();
void getDataAmplificationEEPROM(void);
String getData_toChart(void);
String getCT_toChart(float tmp, char c);
String getResult_toChart(char tmp);

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
uint16_t postData_GoogleSheet(float CT_value[10], char result[10], uint8_t loops);

/**
 * @brief postData_Chart
 *
 */
void postData_Chart(void);

#endif