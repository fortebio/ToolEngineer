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

// Which set of compiled parameter defaults has already been forced into a SAVED config.
// Free slot: ADDR_CHECK_UPDATE holds 4 bytes (228..231) and ADDR_ERROR_NUMBER_UNIT starts at 244.
//
// Why this exists at all: ForteSetting::begin() overwrites the compiled parastructure with the
// EEPROM copy whenever the stored length matches, and sizeof(parastructure) has not changed since
// v2.4.2. So on every unit that has ever been configured, editing a default in this file changes
// NOTHING - the machine keeps 120 rounds / min_increase 20 / min_sharpness 5. That is not a
// cosmetic drift: removed_by_new_gate() compares the legacy thresholds against the RUNTIME ones,
// so with the old values loaded the two sides are equal and the review gate can never fire. Both
// builds would run silently with the feature inert.
//
// Bump CONFIG_REV_THRESHOLDS to force a later set. Do NOT reuse this byte for anything else.
//
// rev 1  first v2.4.3a set - 30 min, min_increase 25, min_sharpness 11, baseline 2-4
// rev 2  min_sharpness 11 -> 8. REQUIRED, not cosmetic: rev 1 shipped to RPL03001 and RPL03002
//        on 15 Aug and the stamp is what stops a migration re-running, so those two units would
//        have kept 11.0 forever - and configSelfCheck would have reported PASS while grading
//        against a yardstick nobody was using.
#define ADDR_CONFIG_REV 236
#define CONFIG_REV_THRESHOLDS 2

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
  // This is the ONLY min_increase that reaches a call - DataIn::fromEEPROM() always runs
  // parameters.fromEEPROM(), which copies from here, so the value in AlgoData::clear() and the
  // one in Algo.h's strJson fixture are both overwritten before any curve is scored. They have
  // been aligned anyway so the next reader is not misled.
  //
  // 20.0 -> 25.0. A DELIBERATELY MODEST floor. An earlier revision set this to 30 on the strength
  // of a duplicate-agreement analysis that assumed slot k pairs with slot k+5. That pairing does
  // NOT exist - every slot is an independent assay - so the analysis and its "61% fleet baseline"
  // were both withdrawn. Do not reintroduce either.
  //
  // What replaced it: 68 curves labelled by eye (real / suspect / non-specific), scored blind.
  // Counted increase orders 9 of 88 real-vs-non-specific pairs backwards; steepness orders 2.
  // Size is the weaker signal, so it is kept as a low floor and min_sharpness carries the rule.
  // Measured over 25,219 wells on a 30-minute run: 25 removes 79 Positives (2.2%), and no well
  // labelled real or suspect is among them.
  //
  // NOT the break threshold. check_breakData() used to be handed this same value; it now takes
  // BREAK_JUMP_THRESHOLD (Algo.h), pinned at 20.0, so moving this cannot retune the jump detector.
  double min_increase = 25.0;             // fluorescence level threshold
  // 5.0 -> 8.0. At 5.0 this test never fired: the classifier reaches it only on curves that
  // already have a peak, and essentially all of them exceed 5. It was inert, not lenient - it
  // removed 1 of 37 labelled non-specific curves.
  //
  // An earlier revision set this to 11.0 on the 68 eye-labelled curves alone. That was too high.
  // On 15 Aug two lab plates with a KNOWN layout (RPL03001/RPL03002, positive control + positive
  // TPD + negative TPD + blanks) put a confirmed positive TPD at steepness 8.6 - RPL03002 slot 2,
  // increase 73.0, larger than either positive control on its own plate, with a clean plateau. At
  // 11.0 the machine called it Negative. A missed detection on a confirmed positive.
  //
  // The mechanism matters more than the number: steepness measures how FAST a reaction runs, and
  // that is mostly template concentration. Raising this gate always removes the weak end of the
  // positives first - the samples the assay exists to catch. Keep it a floor, not the rule.
  //
  // Where 8.0 comes from, against every labelled curve and every lab-confirmed well:
  //     threshold   labelled REAL kept   labelled NSA removed   15 Aug confirmed
  //        6.5          18/18                 21/37                 no errors
  //        8.0          18/18                 27/37                 no errors
  //        8.5          18/18                 28/37                 no errors
  //        9.0          18/18                 28/37                 MISSES RPL03002 s2
  //       11.0          16/18                 31/37                 MISSES RPL03002 s2
  // Removal saturates around 8.5; past it the cost is paid in real curves for nothing. 8.0 sits
  // 0.6 below the weakest confirmed positive and 4.0 above the strongest confirmed negative.
  //
  // Caveats worth keeping. That 8.6 is n=1 - more weak positives should set this, not arithmetic
  // on one well. Steepness is dF/dt on CALIBRATED units, so a drifting per-slot slope moves it;
  // machine removal rates run 0-46% and correlate 0.45 with the machine's median slope, so
  // calibration contributes but does not drive it. Anything landing between 8 and 12 is exactly
  // what the F state in v2.4.3AT is for - flag it, repeat it, do not silently drop it.
  //
  // Also tested and REJECTED, do not reintroduce without new evidence:
  //   settle ratio (final rate / peak rate) - 174 of 666 ranking errors against 23 for steepness,
  //     where chance is 333. It cannot tell a finished reaction from a decaying one: 12 of 37
  //     non-specific curves peak then photobleach, which reads identically to "the reaction ended".
  //   steepness OR settle (pass either to be Positive) - strictly dominated. Every variant removes
  //     FEWER non-specific curves than simply lowering this threshold, and still loses a real one.
  double min_sharpness = 8.0;             // amplification steepnes level
  double min_slight_positive_time = 22.0; /*threshold for calling Slight Positive from Positive*/
  bool detect_shape = true;               // lag phase detection On/Off
  double detection_margin_time = 4.0;     // minimum main peak position to consider Ct value as positive
  // v2.4.3a: 0.9 -> 0.5. At 0.9 the arm search measured a sliver just below the peak: across
  // 4,000 calibrated curves the resulting width took only 15 distinct values, so the shape test
  // separated nothing. At 0.5 it spans the actual exponential phase, and the width becomes
  // diagnostic - under 1 min is a sensor transient, over 8 min is a slow non-specific rise.
  double arm_percentile = 0.5;            // percentile used for calculating lag phase
  double transition_percentile = 0.4;     // percentile used for calcuating transition time (Ct) &
                                          // fluorescence increase
  uint8_t sg_order = 2;                   // interpolation smoothing order
  uint8_t sg_window = 4;                  // smoothing window size for algorithm
  // v2.4.3a: 3-7 min -> 2-4 min. The old window ran three minutes past the earliest Ct the
  // algorithm will accept, so an early amplifier had its zero measured against its own rise:
  // baseline noise read 10.90 on early positives against 1.63 on late ones, a 6.7x difference
  // caused by the window and not by the sample. The window must END where the earliest legitimate
  // Ct begins, so baseline_start + baseline_range == detection_margin_time (2 + 2 == 4.0).
  // The start of 2 min matches the web chart, where optical warm-up was measured to complete
  // by ~2 min - see docs/history/2026-08-02-chart-baseline-start-window.md.
  uint8_t baseline_start = 2;             // start of baselining (minutes)
  uint8_t baseline_range = 2;             // range of baselining (minutes)

  // Device info
  char units[10] = "nM FAM";  // Units, "units"
  // EMPTY, not "RPL": a compiled default that looks like a serial is one a machine will happily
  // upload under. Empty -> sanitiseDeviceId() reports "UNSET" and the operator is prompted.
  // ("RPL" is also what the v2.4.2 portal pre-filled - see idIsPlaceholder in ForteSetting.cpp.)
  char device_id[10] = ""; // device id, "device_id"

  // Opto measurement configuration
  uint16_t lysisDuration = 600;           // duration of lysis, "lysis duration"
  // LED + opto sensor preheat, in SECONDS. "opto preheat time".
  // PREHEATLOOPS = optopreheatduration * 1000 / timePerLoop, so 900 s = 45 rounds of 20 s.
  //
  // READ THIS BEFORE ASSUMING IT GATES ANYTHING. The opto preheat clock starts at BOOT, not when
  // the operator asks for a run: sensor6035::begin() sets sensorStep = eSensorpreheat, and
  // setStepeSensorpreheat() - the call sitting next to timeStartWait = millis() in button.cpp -
  // is `if (sensorStep == eSensorwait)`, which only CALIBRATION ever produces. So on every
  // normal power cycle those calls are no-ops and the optics finish preheating ~900 s after
  // boot regardless of when RED is pressed.
  //
  // PIDControl.cpp's transition needs the hotlid wait (timeStartWait + hotlidWaitMs, and
  // timeStartWait is millis()-at-button or 0) AND getSensorPreheatReady(). Both land at or after
  // boot + 15 min, with the hotlid always last, so THIS VALUE DOES NOT DECIDE WHEN A RUN MAY
  // START - raising it from 300 to 900 on 2026-08-05 changed no observable timing. What it
  // changes is how long the machine counts preheat rounds after boot; eSensormaintain then runs
  // the identical LED cycle minus the counter, so the optics are not treated differently either.
  //
  // Making the sync real would mean letting RED re-arm the clock (relax that eSensorwait guard),
  // which is a behaviour change in its own right - the lysis path (timeStartWait = 0) would gain
  // a 15-minute wait it has never had.
  //
  // Was written `15 * 20` = 300 s, which read as "15 minutes" to everyone who met it and which
  // sensor6035.h stated outright. It was 5 minutes. Now it is 900.
  uint16_t optopreheatduration = 15 * 60;

  uint LEDDuration = 2 * 100;       // LED(time in ms) is on for 0.2s before sensor
                                    // reading###"LED Duration"
  ulong timePerLoop = 20 * 1000;    // Duration(ms) of 1 loop ###"time per loop"
  // v2.4.3a: 120 -> 90 rounds. At timePerLoop = 20 s that is 40 min -> 30 min, and every derived
  // timer follows from it (AMPLIFICATION_DURATION, OPTO_DURATION_2, the TFT countdown, the
  // uploaded metadata) so there is no second place to change.
  //
  // Cost, measured over 25,219 wells: 62 Positives stop being Positive. The large majority are
  // slow drifts whose entire rise happens in the last ten minutes and never resolves into a step.
  // Six across eight months were genuine late amplifications - and at the 30-minute mark those
  // six had reached increases of 4 to 19, all below even the old threshold of 20, so nothing
  // visible on screen is being cut off.
  //
  // Ceiling is 130 (sensor67Value[10][130]); handleConfigPost and the Profile card both clamp.
  uint8_t amplification_time = 90; // quantity to measure during the amplification, "amplification_time"

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
      : 0
#define dbg_sensor(format, ...)                                \
  (cSensorDebug & cDebug)                                      \
      ? DEBUG_COM.printf(HEADER_FORMAT(format), ##__VA_ARGS__) \
      : 0
#define dbg_button(format, ...)                                \
  (cButtonDebug & cDebug)                                      \
      ? DEBUG_COM.printf(HEADER_FORMAT(format), ##__VA_ARGS__) \
      : 0
#define dbg_display(format, ...)                               \
  (cDisplayDebug & cDebug)                                     \
      ? DEBUG_COM.printf(HEADER_FORMAT(format), ##__VA_ARGS__) \
      : 0
#define dbg_bluetooth(format, ...)                             \
  (cBlueToothDebug & cDebug)                                   \
      ? DEBUG_COM.printf(HEADER_FORMAT(format), ##__VA_ARGS__) \
      : 0

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

// Duty ceiling for the two top hotlid PIDs (myPIDhotlid2/3). PID_v1 defaults to 0..255, i.e. no
// ceiling at all, so the controller asks for full duty whenever it sits more than a few degrees
// under the 75 C target - which is most of the heat-up. 128 halves the peak while staying clear
// of the drive the lids actually need: the pre-PID design held this same target with a fixed HIGH
// of 100 (hotlidPWM in parastructure, now unused).
// Do NOT lower this without measuring. A ceiling too low does not merely heat slowly:
// heatNewLid23() raises topHeater2Flag/topHeater3Flag only within 3 C of target, so a lid that
// cannot reach it leaves the machine waiting forever instead of advancing to the amp-tube prompt.
// Compile-time on purpose - a hardware protection limit, not an operating parameter. If it ever
// needs field tuning, parameter.empty[0] is the spare slot (parastructure is at 400 of its 402 B,
// so no new field can be added).
#define HOTLID_PWM_MAX 128

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
                          // PC

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
// MERGE v2.4.4 + v2.4.3AT. Two axes kept independent, as on v2.4.3AT: the shape rule is a
// -D switch, the version string can be overridden per PlatformIO env.
//   <name>AT  default                 a shape-flagged Positive is REPORTED as F, call stands
//   <name>a   -DSHAPE_RULE_NEGATIVE   a shape-flagged Positive is called NEGATIVE
//
// FirmwareVer is MATCHED AGAINST THE .bin FILE NAME the server offers (updateOTA.cpp
// checkFirmware, v2.4.4): `name != "fbt_" + FirmwareVer + ".bin"`, an EXACT match. Two
// consequences for this merged build:
//  1. Never rebuild under a version string already handed to a machine - the fleet would read
//     "You have the lasted version" forever, silently. That already happened at v2.4.5, and it
//     is why NEITHER string below may ever be the bare "v2.4.5": a stale pre-?ver= image is
//     already published as "fbt_v2.4.5.bin", so a build calling itself "v2.4.5" would match
//     that file exactly and be un-updatable over the air for the life of the unit. The suffix
//     is load-bearing here, not decoration - and the #ifndef above lets a PlatformIO env
//     override it, so the rule has to be stated, not just implemented.
//  2. ⚠ A trial machine running this build POLLS every 6 h (v2.4.4 dashboardLoop) and the
//     server's plain "fbt_<ver>.bin" will not match this name -> the machine is offered the
//     NON-AT build and an operator pressing RED replaces the trial firmware. Either keep these
//     units off the network, or give the server a file named for this exact string.
#ifndef FIRMWARE_VERSION
#ifdef SHAPE_RULE_NEGATIVE
#define FIRMWARE_VERSION "v2.4.5a"
#else
#define FIRMWARE_VERSION "v2.4.5AT"
#endif
#endif
static String FirmwareVer = FIRMWARE_VERSION;

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