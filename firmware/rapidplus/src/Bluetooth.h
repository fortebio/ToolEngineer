#ifndef BLUETOOTH_H
#define BLUETOOTH_H

#include "define.h"
#include "BluetoothSerial.h"
#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif

struct parastructure;               //forward declaration
extern String  ssid;
extern String  password;
extern String id;

extern BluetoothSerial SerialBT;

void connectBLE();
void BLEloop();
// void saveParaToEEPROM();
void saveJsonToEEPROM(char * jsondata, uint32_t jsonlen);
void readEEPROM();
void paraDisplay(parastructure);
void loadParaFromEEPROM();
void loadJsonFromEEPROM();
bool loadJsonFromEEPROM(char * jsondata);

void connectWIFI();
void saveCredentialsToEEPROM();
void loadCredentialsFromEEPROM();
void Write_language_ToEEPROM();
void Read_language_fromEEPROM();




#endif