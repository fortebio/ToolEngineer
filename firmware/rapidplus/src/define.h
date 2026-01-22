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
 *
 * @change log:
 *
 * - v1.48
 * PID parameter output
 * PID parameter clear when rerun
 * output Json data with pararead command
 * error msg optimising
 * change PCB version to V1.3 by default
 * opto sensor optimising on error process
 * display firmware version at the function selection display
 * - v1.46
 * Support the loops to 40, so the configuration, memory and FLASH need to be
 * upgraded
 * - v1.45
 * Output log when it's overheat and underheat
 * - v1.44:
 * Update the temperature control logic of top heater during amplification
 * Output pid parameter at command "pararead"
 * - v1.43:
 * 1. Update file PIDControl.cpp
 * Unify the temperature+PID output format.
 * Format is: "Timexxx" + Time(in second) + Heater No. +
 * Reading/Heating/Maintain + Temperature value/(Target temperature+PWM) How to
 * use it:
 * 1. Connect device and open the SerialDebug tool
 * 2. Connect the correct port, clear the old log
 * 3. Send command "TemperatureOutput", then device will output the temperature
 * reading and PWM at any stage
 * 4. Operat the machine to finish the different stage
 * 5. Copy or save all the log data
 * 6. Paste the data into Excel
 * 7. Filter all the colums
 * 8. Choose different heater at the 3rd column, then you can get the
 * temperature value
 *
 * Output data example, no need to know the details, it can be understand easily
 * by real log data e.g.1  TimeRB	1173.81	BottomHeater
 * Reading	32.19	65.5	65.38 1st item: "TimeRB" means
 * Time+Reading+BottomHeater; other: "TimeRT"" means Time+Reading+TopHeater 2nd
 * item: "1173.81" means time in second 3rd item: "BottomHeater" is the name of
 * all the bottom heaters. Other value: "TopHeater" means all top heaters; 4th
 * item: "Reading" is the status; 5th item: Temperature value.
 *
 * e.g.2  TimeMB2	1168.10	Heater2	Maintain	Temperature	65.5
 * Target	65.00	PWM	0 1st item: "TimeMB2" means
 * Time+Maintain+BottomHeater+No.(1-3); other value is "TimePT2", meaning
 * Time+Preheat+TopHeater+No.(1-4) 2nd item: "1168.10" means time in second 3rd
 * item: "Heater2" means BottomHeater+No.(1-3). Other value: "TopHeater3" means
 * TopHeater+No.(1-4) 4th item: "Maintain" means the maintain temperature
 * status; other value: "Heating" means preheating 5th item: "Temperature" means
 * the following item is temperature value 6th item: Temperature value 7th item:
 * "Target" means the following item is target temperature value 8th item:
 * Target temperature value 9th item: "PWM" means the following item is PWM
 * value 10th item: PWM value
 *
 *
 * - v1.42:
 * 1. Time output with temperature value by command "TemperatureOutput"
 * 2. New PID value
 * 3. Add threshold for top heater
 * 4. Adjust the overheat parameter
 * 5. Variable conflict between main loop and each channel testing
 * 6. PID and overheat is configurable: Add PID1&PID2 para, overheat temperature
 * value for bottom and top
 * 7. Adjust the para configuration file. Make sure seq of thermometer is zero.
 * Prepare for different scenarios, especially the sequence of the thermal
 * sensor
 *
 * - v1.41:
 * Update PID parameter gotten from heat stress testing
 * Error process, can only restart the power after error happened
 * More error detection on temperature reading by thermometer
 * Alg bug fixings
 *
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
// #define _EEPROM_SIZE 8192              // add additional for para, record and json file storage.
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
  // int sg_window_display = 2;              // window size for display to users
  uint8_t baseline_start = 3; // start of baselining (minutes)
  uint8_t baseline_range = 4; // range of baselining (minutes)

  // Device info
  char units[10] = "nM FAM";      // Units, "units"
  char device_id[10] = "proto 0"; // device id, "device_id"

  // Opto measurement configuration
  uint16_t lysisDuration = 600;           // duration of lysis, "lysis duration"
  uint16_t optopreheatduration = 15 * 20; // duration for LED and opto sensor preheat in second. "opto
                                          // preheat time"

  uint LEDDuration = 2 * 100;       // LED(time in ms) is on for 0.2s before sensor
                                    // reading###"LED Duration"
  ulong timePerLoop = 20 * 1000;    // Duration(ms) of 1 loop ###"time per loop"
  uint8_t amplification_time = 120; // quantity to measure during the amplification, "amplification_time"

  // heater configuration
  float lysisTemp = 82.0;                     //"lysis temperature"
  float amplifTemp = 65.8;                    //"amplification temperature"
  uint8_t bottomTemperatureSensorSq[3] = {0}; // bottom sensor 1, 2, 3. to be zero by default, need to calibrate it.
  uint8_t topTemperatureSensorSq[3] = {0};    // hotlid sensor 1, 2, 3, ambient sensor. to be zero by default, need
                                              // to calibrate it.
  // double kpid[3] = {40, 1, 20};                   // PID parameter for bottom heater1(Lysis)
  // double kpid2[3] = {60, 0.1, 40};                // PID parameter for bottom heater2&3(Amplification)
  double kpid[3] = {30, 0.05, 30};                // PID parameter for bottom heater1(Lysis)
  double kpid2[3] = {35, 0.1, 40};                // PID parameter for bottom heater2&3(Amplification)
  double bottomOverheat[3] = {5, 5, 5};           // overheat value of bottom heater,
                                                  // underheater value is negative of overheat
  double topOverheat[2] = {20, 20};               // overheat value of top heater
  float temperatureOffset[6] = {0};               // temperature offset of bottom sensor 1, 2, 3, hotlid sensor 1, 2, 3,
                                                  // ambient sensor, the usage is reading temperature + this value ->
                                                  // output temperature
  uint8_t hotlidPWM[2][2] = {{40, 90}, {40, 90}}; // PWM low and high value for hotlid
  uint8_t buzzerOn = 1;                           // on/off status, on is 1 while off is 0. "buzzer" "On"
  double kitId = 0.0;                             // lưu thông tin kid test
  double empty[5] = {0.0};                        // nở vùng dữ liệu để dự phòng
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

// below macro function can support the data print via both of serial port and
// BLE
#define info_displayf(...)         \
  {                                \
    DEBUG_COM.printf(__VA_ARGS__); \
    SerialBT.printf(__VA_ARGS__);  \
  }
#define info_displayln(...)         \
  {                                 \
    DEBUG_COM.println(__VA_ARGS__); \
    SerialBT.println(__VA_ARGS__);  \
  }
#define info_display(...)         \
  {                               \
    DEBUG_COM.print(__VA_ARGS__); \
    SerialBT.print(__VA_ARGS__);  \
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

// Threshold value of overheat and underheat delta value -> move to PIDControl.h
//  #define OVERHEAT_THRESHOLD  2.0           //If temperature is too hot, used
//  for both of Lysis and Amplification #define UNDERHEAT_THRESHOLD  -2.0 //If
//  temperature is not hot enough, used for both of Lysis and Amplification

// before PID control
#define DELTA_FULLPWM 80 // full PWM output when the temperature difference from the target
                         // temperature is lower than it
#define DELTA_HALFPWM 80 // half PWM output when the temperature difference from the target
                         // temperature is lower than it

// target temperature of top heater
// #define HOTLID1_TEMP 60.0
#define HOTLID23_TEMP 70.0

// GPIO used for bottom heater
#define HEATER1IO 33 // heater1
#define HEATER2IO 25 // heater2
#define HEATER3IO 26 // heater3

// GPIO used for top heater 1
// #define HOTLID1IO 5

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
static String FirmwareVer = "v2.3.6"; // add function calib
#endif