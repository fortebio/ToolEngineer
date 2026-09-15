#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include "time.h" // New
#include <HTTPUpdate.h>
#include <EEPROM.h>
#include <Update.h>
#include <ESPmDNS.h>
// #include "displayresources.h"

#ifndef _DEFINE_H
#define _DEFINE_H
#include <Arduino.h>
static String FirmwareVer = {"v2.6.6"};

#define cDebug (1)
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
typedef enum
{
    GROUP_PRAWN,
    GROUP_FISH,
    GROUP_PIG,
    GROUP_COUNT, // tổng số nhóm
    GROUP_CHICKEN,
} group_type;

#define WIFI_PASSWORD "FBT"

#define _EEPROM_SIZE 1024 // add additional for para, record and json file storage.
#define ADDR_SAMPLE_DATA(x) (512 + (100 * x))

#define ADDR_VALUE_CALIB_MIN 0
#define ADDR_VALUE_CALIB_MAX 2
#define ADDR_SLOPE 8
#define ADDR_ORIGIN 12
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
#define ADDR_PASSWORD_CALIB 212
#define ADDR_CHECK_INITIALIZE_SAMPLE (512 - 2)

#define TFT_SCK 18
#define TFT_MOSI 23
#define TFT_MISO 19
#define TFT_CS 22
#define TFT_DC 21
#define TFT_RESET 17

#define ON_LED 25

#define valueMAXsensor 3000
#define valueMinsensor 0

#define NumberButton 3
#define TimePressAnti 50
#define calibTime 5000
#define BUTTON_RED 13
#define BUTTON_BLUE 16
#define BUTTON_WHITE 4

#define settingTime 5000
#define numberCalibration 300

#define POSITIVE_THRESHOLD 500

static String ip = "";

#endif
