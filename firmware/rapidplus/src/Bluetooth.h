#ifndef BLUETOOTH_H
#define BLUETOOTH_H

#include "define.h"
#include "BluetoothSerial.h"
#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif

#include "NTPClient.h"
#include <WebServer.h>
#include <HTTPClient.h>

#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

struct parastructure; // forward declaration
extern String ssid;
extern String password;
// No id_device here any more: the device ID is protoID (_ForteSetting.parameter.device_id),
// a single store as of 2026-07-30. Re-adding a global copy re-creates the drift bug.
extern String id;

extern BluetoothSerial SerialBT;

extern const char *serverName;

extern volatile bool gBtReleased;
void releaseBluetoothStack();

void connectBLE();
// void saveParaToEEPROM();
void readEEPROM();
void paraDisplay(parastructure);
void loadParaFromEEPROM();

void saveSettingDevice();
// Full parameter struct as the JSON shape JsonDataConfig() parses (web GET /config).
String paraToJson(parastructure para);
void loadSettingDevice();
void getDataAmplificationEEPROM(void);

/**
 * @brief googlesheet API
 * @version 2.2
 */
uint16_t postData_GoogleSheet(float CT_value[10], char result[10], uint8_t loops);
// The ONE place that knows the cloud destination list (GAS + ingest + ERP). Both the results
// upload and the error upload go through it, so they cannot drift to different sets again.
// Blocks in TLS: call it with the dashboard suspended.
uint16_t postJsonToAllTargets(const String &jsonPost, const char *what);

#endif