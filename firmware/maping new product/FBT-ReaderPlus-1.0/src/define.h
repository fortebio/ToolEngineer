#ifndef _DEFINE_H_
#define _DEFINE_H_

#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <EEPROM.h>
#include <Update.h>
#include <ESPmDNS.h>
#include <Arduino.h>
#include "time.h"

#define cDebug (0)
#define cMainDebug (1)
#define cSensorDebug (1)
#define cButtonDebug (1)
#define cDisplayDebug (1)
#define cBLEDebug (1)

#define DEBUG_COM Serial
#define HEADER_FORMAT(fmt) "<%s>:<%d> " fmt "\r\n", pathToFileName(__FILE__), __LINE__
#define dbg_main(format, ...) (cMainDebug & cDebug) ? DEBUG_COM.printf(HEADER_FORMAT(format), ##__VA_ARGS__) : NULL
#define dbg_sensor(format, ...) (cSensorDebug & cDebug) ? DEBUG_COM.printf(HEADER_FORMAT(format), ##__VA_ARGS__) : NULL
#define dbg_button(format, ...) (cButtonDebug & cDebug) ? DEBUG_COM.printf(HEADER_FORMAT(format), ##__VA_ARGS__) : NULL
#define dbg_display(format, ...) (cDisplayDebug & cDebug) ? DEBUG_COM.printf(HEADER_FORMAT(format), ##__VA_ARGS__) : NULL
#define dbg_bluetooth(format, ...) (cBLEDebug & cDebug) ? DEBUG_COM.printf(HEADER_FORMAT(format), ##__VA_ARGS__) : NULL

//#define WIFI_SSID "FBT"
#define WIFI_PASSWORD "FBT"

#define _EEPROM_SIZE 512

#define ADDR_OFFSET 8
#define ADDR_VALUE_CALIB_MIN(i) (i * 2)
#define ADDR_VALUE_CALIB_MAX(i) ((i * 2) + ADDR_OFFSET)

#define ADDR_THRESHOLD_BASE 16
#define ADDR_THRESHOLD_POSITIVE(threshold) (ADDR_THRESHOLD_BASE + (threshold * sizeof(uint32_t)))
#define ADDR_LANGUAGE 36
#define ADDR_ID_DEVICE_BASE 40
#define ADDR_SSID 60
#define ADDR_PASSWORD 95
#define ADDR_ID_BLE 130

#define ADDR_CHECK_CALIB 200
#define ADDR_CHECK_THRESHOLD 202 
#define ADDR_CHECK_SLOPE 204 
#define ADDR_CHECK_ORIGIN 206
#define ADDR_CHECK_LANGUAGE 208
#define ADDR_CHECK_ID_DEVICE 210

#define TFT_SCK 18
#define TFT_MOSI 23
#define TFT_MISO 19
#define TFT_CS 22
#define TFT_DC 21
#define TFT_RESET 17

/* define IO LED */
#define LED_1 25
#define LED_2 26
#define LED_3 27
#define LED_4 14
#define PWM   12


#define valueMAXsensor 3000
#define valueMinsensor 0

#define NumberButton 3
#define TimePressAnti 50
#define calibTime 5000
#define BUTTON_RED 13
#define BUTTON_GREEN 16
#define BUTTON_WHITE 4
#define settingTime 5000
#define numberCalibration 300

#define POSITIVE_THRESHOLD 500

static String ip = "";
static String FirmwareVer = {"v1.0"};

#endif
