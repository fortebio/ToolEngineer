#ifndef _DEFINE_H
#define _DEFINE_H

/**
 * @file define.h
 * @brief Main configuration file.
 *
 * @version 1.0.0
 * @date 2024-01-01
 * @author IMT, Dxdhub
 *
 * @details
 * EEPROM format configuration
 * Parameter structure
 * Debug output configuration
 * GPIO definition
 * Opto configuration
 * Temperature configuration
 * Button configuration
 * Buzzer configuration
 * Firmware version configuration
 */

#include "time.h"
#include <EEPROM.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClient.h>

#include "Bluetooth.h"
#include "updateOTA.h"

#include <Arduino.h>

// Format of EEPROM:
// 0~511:    previous Forte setting, 512 bytes
// 512~1023: para with parastructure format, 512 bytes
// 1024~4095: record, 3K

#define _EEPROM_SIZE 4096              // add additional for para, record and json file storage.
#define PARAMETERPOS 512               // Record start at 512 with length to be 1800(store 90 rounds data), the
                                       // first 512 is reserved for Forte to use
#define RECORDPOS (PARAMETERPOS + 512) // parameter start after record, the length of parameter is 336

#define ADDR_LANGUAGE 36
#define ADDR_SSID 40
#define ADDR_PASSWORD 75
#define ADDR_ID_BLE 130
#define ADDR_ID_DEVICE_BASE 170
#define ADDR_CHECK_ID_DEVICE 210
#define ADDR_CHECK_LANGUAGE 220
#define ADDR_CHECK_BT 224
#define ADDR_CHECK_UPDATE 228

#define ADDR_ERROR_NUMBER_UNIT 244 // the size of the error record, used to check if the error record is valid or not. If the value read from EEPROM is not equal to it, then it's not valid, and need to be cleared.
#define ADDR_ERROR_FLAG 250
#define ADDR_ERROR_RECORD (ADDR_ERROR_FLAG + 1) // record the error type and times, used for error process

typedef enum
{
  /*************************************
   * 0: Sensor Error None
   * 1: Sensor Amplification Right
   * 2: Sensor Amplification Left
   * 3: Sensor Heater Right
   * 4: Sensor Heater Left
   * 5: Sensor Heater TopRight
   * 6: Sensor Heater TopLeft */
  errorNone = 0,
  errorAmplificationRight,
  errorAmplificationLeft,
  errorHeaterRight,
  errorHeaterLeft,
  errorHeaterTopRight,
  errorHeaterTopLeft,
} errorModule;

typedef struct
{
  uint8_t errorModule = 0;           /*************************************
                                      * 0: Sensor Error None
                                      * 1: Sensor Amplification Right
                                      * 2: Sensor Amplification Left
                                      * 3: Sensor Heater Right
                                      * 4: Sensor Heater Left
                                      * 5: Sensor Heater TopRight
                                      * 6: Sensor Heater TopLeft */
  uint8_t errorType = 0;             /*************************************
                                      * Sensor Amplification
                                      *   0: No error
                                      *   1: no data from sensor
                                      *   2: not all data from sensors
                                      *   3: wrong data from sensor
                                      *   4: Sensor too dark
                                      *   5: Sensor too bright
                                      * Sensor Heater
                                      *   0: No error
                                      *   1: Overheat
                                      *   2: Underheat
                                      *   3: Heater disconnected
                                      *   4: Wrong data from sensor */
  uint8_t errorSlot = 0xFF;          /* record which slot has error, used for error process. For example, if the error is "no data from sensor", then record which slot has no data, so that we can do more specific error process. The value is the same as the slot number, starting from 0. If it's not related to specific slot, then set it to 0xFF. */
  uint8_t errorProcessStep = 0;      /* record which step of the error process, used for error process. For example, if the error is "no data from sensor", then we can do different process for different step, such as first time, second time, third time, etc. The value is starting from 0, and increase by 1 each time the same error happened. */
  unsigned long long errorTimes = 0; /* times of the same error happened, used for error process */
} ErrorRecord;                       /* define the structure for recording the error type and times, used for opto sensor reading error process */

struct parastructure
{
  int length = 0; // length of the structure, to indicate EEPROM has parameter
                  // or not. Only if the length read from EEPROM equal to the
                  // structure length, then yes. As of 11 Apr, the length is 244

  // Version information
  char para_version[10] = "V1.3"; // change from soft version to para version,
                                  // must include it in the json data!!!
  char PCB_version[10] = "V1.3";  // hardware version to differentiate the different version PCB

  // Opto calibration
  // define the parameter matrix used for result calculation, there are
  // 10 channels, each one include parameter {a,b}, "slopes"
  float slopes[10] = {1, //
                      1, //
                      1, //
                      1, //
                      1, //
                      1, //
                      1, //
                      1, //
                      1, //
                      1};

  float origins[10] = {0}; // origin value, "origins"

  uint8_t led_power[10] = {0x96,  //
                           0x96,  //
                           0x96,  //
                           0x96,  //
                           0x96,  //
                           0x96,  //
                           0x96,  //
                           0x96,  //
                           0x96,  //
                           0x96}; // PWM value to control the LED intensity,
                                  // "led_power"

  // Alg parameter
  double min_increase = 20.0;             // fluorescence level threshold
  double min_sharpness = 5.0;             // amplification steepnes level
  double min_slight_positive_time = 22.0; /*threshold for calling Slight Positive from Positive*/
  bool detect_shape = true;               // lag phase detection On/Off
  double detection_margin_time = 4.0;     // minimum main peak position to consider Ct value as positive
  double arm_percentile = 0.9;            // percentile used for calculating lag phase
  double transition_percentile = 0.4;     // percentile used for calcuating transition time (Ct) &
                                          // fluorescence increase
  uint8_t sg_order = 2;                   // interpolation smoothing order
  uint8_t sg_window = 4;                  // smoothing window size for algorithm
  uint8_t baseline_start = 3;             // start of baselining (minutes)
  uint8_t baseline_range = 4;             // range of baselining (minutes)

  // Device info
  char units[10] = "nM FAM";  // Units, "units"
  // EMPTY, not "RPL": a compiled default that looks like a serial is one a machine will happily
  // upload under. Empty -> sanitiseDeviceId() reports "UNSET" and the operator is prompted.
  // ("RPL" is also what the v2.4.2 portal pre-filled - see idIsPlaceholder in ForteSetting.cpp.)
  char device_id[10] = ""; // device id, "device_id"

  // Opto measurement configuration
  uint16_t lysisDuration = 600;           // duration of lysis, "lysis duration"
  uint16_t optopreheatduration = 15 * 20; // duration for LED and opto sensor preheat in second. "opto
                                          // preheat time"

  uint LEDDuration = 2 * 100;       // LED(time in ms) is on for 0.2s before sensor
                                    // reading###"LED Duration"
  ulong timePerLoop = 20 * 1000;    // Duration(ms) of 1 loop ###"time per loop"
  uint8_t amplification_time = 120; // quantity to measure during the amplification, "amplification_time"

  // heater configuration
  float lysisTemp = 82.0;                           //"lysis temperature"
  float amplifTemp = 65.8;                          //"amplification temperature"
  uint8_t bottomTemperatureSensorSq[3] = {0};       // bottom sensor 1, 2, 3. to be zero by default, need to calibrate it.
  uint8_t topTemperatureSensorSq[3] = {0};          // hotlid sensor 1, 2, 3, ambient sensor. to be zero by default, need
                                                    // to calibrate it.
  double kpid[3] = {30, 0.05, 30};                  // PID parameter for bottom heater1(Lysis)
  double kpid2[3] = {60, 0.1, 40};                  // PID parameter for bottom heater2&3(Amplification)
  double bottomOverheat[3] = {5, 5, 5};             // overheat value of bottom heater,
                                                    // underheater value is negative of overheat
  double topOverheat[2] = {20, 20};                 // overheat value of top heater
  float temperatureOffset[6] = {0};                 // temperature offset of bottom sensor 1, 2, 3, hotlid sensor 1, 2, 3,
                                                    // ambient sensor, the usage is reading temperature + this value ->
                                                    // output temperature
  uint8_t hotlidPWM[2][2] = {{40, 100}, {40, 100}}; // PWM low and high value for hotlid

  uint8_t buzzerOn = 1;            // on/off status, on is 1 while off is 0. "buzzer" "On"
  double kitId = 0.0;              // lưu thông tin kid test
  double kpid3[3] = {60, 0.1, 40}; // PID parameter for Top Hotlibd2&3(Amplification)
  double empty[2] = {0.0};         // nở vùng dữ liệu để dự phòng
};

#define cDebug (0)
#define cMainDebug (1)
#define cSensorDebug (0)
#define cButtonDebug (1)
#define cDisplayDebug (0)
#define cBlueToothDebug (1)

#define DEBUG_COM Serial
#define HEADER_FORMAT(fmt) \
  "<%s>:<%d> " fmt "\r\n", pathToFileName(__FILE__), __LINE__
#define dbg_main(format, ...)                                  \
  (cMainDebug & cDebug)                                        \
      ? DEBUG_COM.printf(HEADER_FORMAT(format), ##__VA_ARGS__) \
      : NULL
#define dbg_sensor(format, ...)                                \
  (cSensorDebug & cDebug)                                      \
      ? DEBUG_COM.printf(HEADER_FORMAT(format), ##__VA_ARGS__) \
      : NULL
#define dbg_button(format, ...)                                \
  (cButtonDebug & cDebug)                                      \
      ? DEBUG_COM.printf(HEADER_FORMAT(format), ##__VA_ARGS__) \
      : NULL
#define dbg_display(format, ...)                               \
  (cDisplayDebug & cDebug)                                     \
      ? DEBUG_COM.printf(HEADER_FORMAT(format), ##__VA_ARGS__) \
      : NULL
#define dbg_bluetooth(format, ...)                             \
  (cBlueToothDebug & cDebug)                                   \
      ? DEBUG_COM.printf(HEADER_FORMAT(format), ##__VA_ARGS__) \
      : NULL

// Set true once the Bluetooth Classic stack has been permanently torn down
// (esp_bt_mem_release). After that point ANY SerialBT call posts to a freed
// bluedroid thread and triggers `assert failed: osi_thread_post (thread != NULL)`,
// which reboots the device. Every SerialBT access below (and in the tasks) is
// gated on this flag. Defined in Bluetooth.cpp.
extern volatile bool gBtReleased;

// Print to both USB serial and Bluetooth SPP. The SerialBT half is skipped once
// releaseBluetoothStack() has run (gBtReleased): after that the BT controller memory
// is freed, so touching SerialBT is use-after-free (GOTCHA 1). The dashboard now
// releases BT at startup on every WiFi boot, so this guard matters device-wide - USB
// serial (DEBUG_COM) stays on regardless.
#define info_displayf(...)          \
  {                                 \
    DEBUG_COM.printf(__VA_ARGS__);  \
    if (!gBtReleased)               \
      SerialBT.printf(__VA_ARGS__); \
  }
#define info_displayln(...)          \
  {                                  \
    DEBUG_COM.println(__VA_ARGS__);  \
    if (!gBtReleased)                \
      SerialBT.println(__VA_ARGS__); \
  }
#define info_display(...)          \
  {                                \
    DEBUG_COM.print(__VA_ARGS__);  \
    if (!gBtReleased)              \
      SerialBT.print(__VA_ARGS__); \
  }

// GPIO used for LCD
#define TFT_SCK 18
#define TFT_MOSI 23
#define TFT_MISO 19
#define TFT_CS 22
#define TFT_DC 21
#define TFT_RESET 17

// GPIO used for I2C
#define SDA_Forte 14
#define SCL_Forte 27
#define I2C_RST 13

// sensor setting
#define ChannelEnableSet ChannelEnableSetBoth
#define ALSITSet ALSITSet100
#define GAINSet GAINSetDouble
#define DGSet DGSetNormal
#define SENSSet SENSSetHigh

#define LOGODISPLAYTIME 1000 // duration of the logo to show

// define the command of the sensor reading and setting
#define ChannelEnableRead "CHANNEL_EN"
#define ChannelEnableSetALS "CHANNEL_EN ALS"
#define ChannelEnableSetBoth "CHANNEL_EN Both"

#define ALSITRead "ALS_IT"
#define ALSITSet25 "ALS_IT 0"
#define ALSITSet50 "ALS_IT 1"
#define ALSITSet100 "ALS_IT 2"
#define ALSITSet200 "ALS_IT 3"
#define ALSITSet400 "ALS_IT 4"
#define ALSITSet800 "ALS_IT 5"

#define GAINRead "GAIN"
#define GAINSetNormal "GAIN Normal"
#define GAINSetDouble "GAIN Double"

#define DGRead "DG"
#define DGSetNormal "DG Normal"
#define DgSetDouble "DG Double"

#define SENSRead "SENS"
#define SENSSetHigh "SENS High"
#define SENSSetLow "SENS Low"

#define SnapShot "Snapshot"

#define PWM_OFF 0 // PWM control, used by heaters
#define PWM_FULL 255
#define PWM_HALF 127
#define PWM_Heater23 150
#define PWM_HOTLIDFULL 110

// GPIO used for LED driver
#define LED_PWM_PORT 4 // control the pwm output for LED driver

// Below is the I/O expander number that links to LED
#define LED0 0
#define LED1 1
#define LED2 2
#define LED3 3
#define LED4 4
#define LED5 8
#define LED6 9
#define LED7 10
#define LED8 11
#define LED9 12

// Definition of the quantity of opto channel. It is fixed to be 10 channels now
#define OPTOCHANNELS 10

// Pin number of IO expander used for buzzer
#define BUZZER 14 // GPB6, gpio of IO expander

// GPIO used for Fan
#define FANIO 12

// before PID control
#define DELTA_FULLPWM 80 // full PWM output when the temperature difference from the target
                         // temperature is lower than it
#define DELTA_HALFPWM 80 // half PWM output when the temperature difference from the target
                         // temperature is lower than it

// target temperature of top heater
#define HOTLID23_TEMP 75.0

// GPIO used for bottom heater
#define HEATER1IO 33 // heater1
#define HEATER2IO 25 // heater2
#define HEATER3IO 26 // heater3

// GPIO used for top heater2&3, PCB V1.1 and V1.2
#define HOTLID23IO 16

// Below is used for PCB V1.3
#define HOTLID2IO 2
#define HOTLID3IO 16 // same as V1.1 and V1.2

// Quantity definition of temperature sensor
#define HEATBLKQUANTITY 3 // 3 bottom temperature sensors
#define HOTLIDQUANTITY 3  // 2 top temperature sensors plus 1 ambient temperature sensor located at
                          // PCB

// Button definition
#define NumberButton 3
#define TimePressAnti 50
#define calibTime 5000

// GPIO used for button
#define BUTTON_RED 39
#define BUTTON_BLUE 34 // or Green
#define BUTTON_WHITE 36

// GPIO definition of temperature sensor
#define ONE_WIRE 32  // temperature sensor used for heat block
#define ONE_WIRE1 15 // temperature sensor used for hot lid and PCB

static String ip = "";
static String FirmwareVer = "v2.4.3"; // add function calib

extern SemaphoreHandle_t gI2CMutex;
extern SemaphoreHandle_t gSPIMutex;
extern SemaphoreHandle_t gEepromMutex;

// Serialize every EEPROM.begin..end section (see gEepromMutex in main.cpp). The Arduino
// EEPROM object holds ONE 4096B heap buffer; begin() reallocs it and end() frees it, so
// two tasks overlapping their begin..end (e.g. a PID safety error-save on ControlTask vs a
// /reviewlast read on SettingTask) double-free that buffer -> the reader gets garbage. Take
// before begin(), give after end(). Null-guarded so it is safe before the mutex is created
// (early boot reads). Sections MUST NOT nest - plain (non-recursive) mutex, portMAX_DELAY is
// fine because every section is a few EEPROM ops (the longest holds it ~200ms across delays).
static inline void eepromLock()
{
  if (gEepromMutex)
    xSemaphoreTake(gEepromMutex, portMAX_DELAY);
}
static inline void eepromUnlock()
{
  if (gEepromMutex)
    xSemaphoreGive(gEepromMutex);
}
#endif