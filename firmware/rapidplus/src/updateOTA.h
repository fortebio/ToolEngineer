#ifndef _UPDATEOTA_H_
#define _UPDATEOTA_H_

#include "define.h"
#include "displayCLD.h"
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include <Update.h>

void checkFirmware(void);
void updateFirmware(void);

extern bool flagUpdate, flag_check_Update;
extern String fwCont, fwVer;

#endif