#ifndef _BLUETOOTH_H_
#define _BLUETOOTH_H_

#include "define.h"
#include "sensor.h"
#include "displayLCD.h"
#include "BluetoothSerial.h"
#include "NTPClient.h"
#include "displayresources.h"
#include <HTTPClient.h>
#include <WiFiManager.h>
#include <WebServer.h>

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif

extern String ssid;
extern String password;
extern String id_BLE;
extern String id_device;
extern BluetoothSerial SerialBT;

void connectWIFI();
void saveCredentialsToEEPROM();
void loadCredentialsFromEEPROM();

void settingBLE_ThresholdPositive(void);
String getName_ThresholdPositive(sick_type sick);
uint32_t getData_ThresholdPositive(sick_type sick);
float EnterCalib(void);

//void connect_GoogleSheet(void);
void postData_GoogleSheet(sick_type sick, uint32_t val_sensor1, uint32_t val_sensor2, uint32_t val_sensor3, uint32_t val_sensor4, String id);

//void Wifi_autoConnect(void);
void settingWifi(void);
void settingUpdate(void);
void settingThreshold(void);

#endif