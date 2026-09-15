#ifndef _SENNSOR6035_H
#define _SENNSOR6035_H
// This is new opto sensor VML6035 to replace the old sensor TCS34725

#include "./VEML6035/VEML6035.h"
#include "./VEML6035/VEML6035_Prototypes.h"
#include "./VEML6035/typedefinition.h"
#include "./VEML6035/VEML6035_Application_Library.h"
#include "TCA9548A.h"
#include "define.h"
#include <Arduino.h>
#include <vector>
#include <ArduinoJson.h>
#include "ForteSetting.h"
#include "Alg/Algo.h"
#include "Alg/sgsmooth.h"
#include "acquisition.h"
// #include "Alg/para.h"

#define Config_ALS_IT VEML6035_ALS_IT_100ms
#define Config_GAIN VEML6035_GAIN_1_DOUBLE
#define Config_DG VEML6035_DG_0_NORMAL
#define Config_SENS VEML6035_SENS_0_x1
#define Config_CHANNEL VEML6035_WHITE_CH_EN

typedef enum
{
    eSensorwait,       // wait before it starts to work
    eSensorpreheat,    // 45 rounds = 900 s, counted from BOOT (begin() enters this step; the
                       // setStepeSensorpreheat() calls on the button paths are no-ops because
                       // they guard on eSensorwait, which only calibration produces). It is
                       // therefore never the condition that decides when a run may start - the
                       // hotlid wait always lands later. See define.h. Was 300 s until
                       // 2026-08-05, whatever the old "15 mins" comment here claimed.
    eSensormaintain,   // maintain after preheating
    eSensorstart,      // start to initialize the opto
    eSensor1stReading, // start to read the 1st stage opto data

    eSensorcalib // calib
} e_sensorStep;

class sensor6035
{
private:
    // I2C multiplex channel number to sensor number(0~4)
    uint8_t I2C_Channel[5] = {6, 0, 1, 2, 3}; // by default is old version(PCB V1.2), the sequence of new one is {6, 5, 1, 2, 3};
    TCA9548A I2CMux;                          // Address can be passed into the constructor
    TCA9548A I2CMux1 = TCA9548A(0x74);        // Address can be passed into the constructor

    unsigned long START_INTERVAL_TIME = 0; // this is to record the start time of 1 round/loop reading

    uint8_t COUNTER = 0;
    // Rounds the LAST finished run had. COUNTER is zeroed the moment amplification
    // completes, but sensor67Value keeps the curve, so this retains its length for
    // post-run read-back (web /curve). Cleared by clear() when a new run starts.
    uint8_t lastRunLoops = 0;
    bool flagCounterDisplay = false; // control to display counter or timeZ
    e_sensorStep sensorStep = eSensorwait;
    uint8_t iChannel = 0; // record the channel that is reading

    // variables for testing LOD
    int _heater1_PWM = 0;
    int _heater2_PWM = 0;
    int _heater3_PWM = 0;

    int _top_heater2_PWM = 0;
    int _top_heater3_PWM = 0;

    bool bSensorReadingFlag = false; // flag to inform heater to on/off hotlid23 heating, enable only when the sensor is reading, disable when it's not reading.
    unsigned long tic;

public:
    // Zero-init: the buffer is cleared at the start of each run and filled during it (see
    // CLAUDE.md GOTCHA 7). The old {1,2,..40} demo ramp only "looked like test data".
    Word sensor67Value[10][130] = {};

    sensor6035(/* args */);
    ~sensor6035();

    void begin();
    void loop();

    void skip2Maintain();
    void rerun();

    void setStepeSensorpreheat();
    void setStepeSensorstart();

    void clear(); // add later after supporting both of 5 and 10 channels

    void reConfigSensors();
    void reConfigSingleSlotSensor(uint8_t slot)
    {
        closeSensorChannel(slot);
        delay(20);
        ResetSingleSensor(slot);
        delay(20);
        reConfigSingleSensor(slot);
    }

    void testShot(int slot);

    /* Function calib */ //////////////////////////////////////
    float calib_sensor(int slot);
    void calibration(int slot);
    float result_calib[4] = {0, 0, 0, 0};
    int type_calib = 0;
    float cal_calib[3] = {0.0};
    void calculate_calib(float y[]);
    void setStepeSensorwait();
    void setStepeSensorcalib();

    void OptoCommandProcess(char command);

    bool bSensorReadingGet();

    // Current amplification round index (0..MEASUREMENTLOOPS). The latest
    // completed round is getCurrentLoop()-1; used by the web dashboard to stream
    // one live chart point per completed round. Reads 0 once the run finishes.
    uint8_t getCurrentLoop() { return COUNTER; }

    // Rounds of the last finished run (0 if none / a new run has started). Lets the
    // web serve the stored curve after COUNTER is zeroed at the end of a run.
    uint8_t getLastRunLoops() { return lastRunLoops; }
    // Set when re-loading a stored run from EEPROM for review (the record has no length
    // of its own, so the caller passes amplification_time). Lets /curve serve it.
    // Ignores 0 ON PURPOSE. Every publisher derives n from scanRunLength(), and a caller that
    // happens to scan an empty buffer must not erase a length another path published correctly
    // moments earlier. Measured twice on RPL03018: /curve went 12 -> 0 about 15 s after a
    // correct publish, because a /reviewlast queued while the run was still going only drained
    // once the device went idle and by then scanned nothing. The result - /slots ready=true
    // with /curve count=0 - is unrecoverable from the browser (the client only reloads from
    // EEPROM when ready==false), which is why the chart came back only after a power cycle.
    // clear() still resets the field DIRECTLY at run start, so a real new run zeroes it.
    void setLastRunLoops(uint8_t n)
    {
        // TEMPORARY (2026-07-28): /curve keeps going back to 0 while /slots stays ready=true,
        // which the browser cannot recover from. Four attempts at inferring the writer were
        // wrong, so every write announces itself now - the next occurrence names its caller
        // instead of costing another guess. Remove once the culprit is fixed.
        Serial.printf("[len] set(%u) was=%u %s\n", n, lastRunLoops,
                      n ? "APPLIED" : "IGNORED(zero)");
        if (n)
            lastRunLoops = n;
    }

    // Real length of the run currently in sensor67Value. The record carries no length of its
    // own, and amplification_time must NOT be trusted (config can change between the run and
    // reading it back -> /curve reads past the data into garbage, or truncates it). Scan for
    // the last round whose slot-0 raw is plausible: rounds past the run read 0 (run-end
    // zero-inits the staging buffer) or 0xFFFF (virgin EEPROM).
    // Guarded host-side by tools/test_curve_length.cpp.
    uint8_t scanRunLength()
    {
        uint8_t len = 0;
        for (uint8_t j = 0; j < 130; j++)
        {
            uint16_t v = sensor67Value[0][j];
            if (v > 10 && v < 60000)
                len = j + 1;
        }
        return len;
    }

    bool bResultGet(float *CT_value, char *result);
    bool bResultPutToGoogleSheet(float *CT_value,
                                 char *result,
                                 struct DiagnosticOutcome *get_outcome,
                                 struct FeatureDetection *get_peak_features);

    void AlgLoop(char *recvData);

    void eSensorParaIni();
    bool getSensorPreheatReady();

    void setCounterDisplayflag(bool flag);

    void outputHeader();
    float calCalibratedValue(int channel, int cnt);

private:
    void Basic_Initialization_Auto_Mode();
    void setI2CChannelSeq();
    void ResetAllSensors();
    void ResetSingleSensor(uint8_t slot)
    {
        openSensorChannel(slot);
        /* Reset Sensor to default value */
        Reset_Sensor();
        Basic_Initialization_Auto_Mode();
        closeSensorChannel(slot);
    }
    void ChannelEnableProcess_loop();
    void ChannelEnableProcess(String command);
    void ALS_IT_Process_loop();
    void ALS_IT_Process(String command);
    void GainProcess_loop();
    void GainProcess(String command);
    void DigitalGainProcess_loop();
    void DigitalGainProcess(String command);
    void SENS_loop();
    void SENS(String command);
    void Snapshot_loop_test();
    void Snapshot();

    void eSensorPreheat();
    void eSensorMaintain();
    void eSensorstartFunc();
    void eSensor1stReadingFunc();
    void closeSensorChannel(int slot);
    void openSensorChannel(int slot);

    bool checkSensorConfiguration();
    void reConfigSingleSensor(int slot);

    unsigned long sensor67ValueTime = 0;

    uint8_t errCnt = 0;
    uint8_t oddCnt = 0;
    unsigned long errRereadTime = 0;
    uint16_t errRecord[10][3] = {0};
    AcquisitionControl acquisitionControl = AcquisitionControl();

    // std::vector<double> integratedResponse;
    // bool flagCalibSensor = false;
    // bool flagformatCalib = false;
    // bool flagback = false;
};

// LED ALS variable, configurable from para structure
#define MEASUREMENTLOOPS _ForteSetting.parameter.amplification_time
#define LED_DELAY_TIME _ForteSetting.parameter.LEDDuration
#define LYSIS_DURATION _ForteSetting.parameter.lysisDuration // Duration of lysis in second.
#define OPTO_INTERVAL _ForteSetting.parameter.timePerLoop    // Duration per session
#define PREHEATLOOPS (_ForteSetting.parameter.optopreheatduration * 1000 / OPTO_INTERVAL)
#define AMPLIFICATION_DURATION (MEASUREMENTLOOPS * OPTO_INTERVAL)
#define OPTO_DURATION_2 AMPLIFICATION_DURATION

#define FORTE_SLOPES _ForteSetting.parameter.slopes
#define FORTE_ORIGINS _ForteSetting.parameter.origins

extern sensor6035 _sensor6035;
#endif