#ifndef _PIDCONTROL_H
#define _PIDCONTROL_H

#include <PID_v1.h>
#include "thermometer.h"
#include "displayCLD.h"
#include "buzzer.h"
#include "sensor6035.h"
#include "ForteSetting.h"

// Threshold value of overheat and underheat delta value -> move to PIDControl.h
#define OVERHEAT_THRESHOLD1 _ForteSetting.parameter.bottomOverheat[0] // If temperature of bottom heater1 is too hot
#define OVERHEAT_THRESHOLD2 _ForteSetting.parameter.bottomOverheat[1] // If temperature of bottom heater2 is too hot
#define OVERHEAT_THRESHOLD3 _ForteSetting.parameter.bottomOverheat[2] // If temperature of bottom heater3 is too hot
// #define OVERHEAT_THRESHOLD_TOP1  _ForteSetting.parameter.topOverheat[0]           //If temperature of top heater1 is too hot
#define OVERHEAT_THRESHOLD_TOP2 _ForteSetting.parameter.topOverheat[0] // If temperature of top heater2 is too hot
#define OVERHEAT_THRESHOLD_TOP3 _ForteSetting.parameter.topOverheat[1] // If temperature of top heater3 is too hot
#define UNDERHEAT_THRESHOLD1 -1 * OVERHEAT_THRESHOLD1                  // If temperature of bottom heater1 is too low
#define UNDERHEAT_THRESHOLD2 -1 * OVERHEAT_THRESHOLD2                  // If temperature of bottom heater2 is too low
#define UNDERHEAT_THRESHOLD3 -1 * OVERHEAT_THRESHOLD3                  // If temperature of bottom heater3 is too low
// #define UNDERHEAT_THRESHOLD_TOP1  -1*OVERHEAT_THRESHOLD_TOP1         //If temperature of top heater1 is too low
// #define UNDERHEAT_THRESHOLD_TOP2  -1*OVERHEAT_THRESHOLD_TOP2         //If temperature of top heater2 is too low
// #define UNDERHEAT_THRESHOLD_TOP3  -1*OVERHEAT_THRESHOLD_TOP3         //If temperature of top heater3 is too low

typedef enum
{
    epidready,           // ready to start
    epid1startpreHeat80, // make sure it's not overheat
    epid1preheat80,      // heat heat1 to 80
    epid1ready, // lysis is ready
    epid2startpreHeat67, // check before preheat heater2 to 67
    epid2preHeat67,      // preheat heater2 to 67
    epid3startpreHeat67, // preheat heater3 to 67
    epid3preHeat67,      // preheat heater3 to 67
    ehotlid23heat,       // heat hotlid2
    epid23ready,         // heater2 and heater3 are ready, wait for next step
    epidcalibpreheat55,  // calib flow: preheat heater2,3 to 55C then 5-min hold
    epidcalibmaintain55  // calib flow: hold heater2,3 at 55C during select/calib
} e_pidstep;

class PIDControl
{
private:
    /* data */
    // All PID variable, configurable
    double bottomTemperature[HEATBLKQUANTITY] = {0.0}; // store the bottom sensor+offset value
    double HotlidTemperature[HOTLIDQUANTITY] = {0.0};  // store the top sensor+offset value
    e_pidstep pidStep = epidready;

    // PID input value
    double TARGET_TEMP = 0; // = LYSIS_TEMP;
    double CURRENT_TEMP_PID = 0;
    // PID output value
    double RESPONSE_SIGNAL = 0.0;
    // PID definition
    PID *myPID; // change to pointer, so it can be initialized with configurable PID parameter
    PID *myPID2;
    PID *myPID3;
    PID *myPIDhotlid2; // PID for top hotlid2 (Amplification), use kpid3
    PID *myPIDhotlid3; // PID for top hotlid3 (Amplification), use kpid3
    // safety check variable
    unsigned long bottomSensorRespTime = 0; // timer to record when the bottom temperature sensor should response the value
    unsigned long topSensorRespTime = 0;    // timer to record when the top temperature sensor should response the value

    unsigned long START_INTERVAL_TIME = 0;

    // Calib-preheat (55C) flow timers
    uint32_t timeCalibReached = 0; // millis() when heater2,3 first reached 55C
    bool bCalib55Reached = false;  // latched once 55C reached, starts the 5-min hold

    // flag to control the simulation enable or disable
    bool bheater1Simu = false;
    bool bheater2Simu = false;
    bool bheater3Simu = false;
    // bool bhotlid1Simu = false;
    bool bhotlid23Simu = false;

    bool btemperatureOut = false;

public:
    // bool waitWarmAmpTube = false; // after preheat the sensor, wait for the amp tube to be put in and heat up to 67 degree
    uint32_t timeStartWait = 0;
    // Wait after both hotlids reach temp before showing the amp-tube prompt.
    // Default 15 min (normal lysis->amp run); the calib->amp path sets it to 5 min.
    uint32_t hotlidWaitMs = 15 * 60000;
    PIDControl(/* args */);
    ~PIDControl();
    void begin();
    void loop();
    void rerun();

    void sensorSeq();

    void timeoutSetting();

    void heatSimulation(int type);
    void RevTemperatureOutput();
    void setPID23Ready();
    void temperatureSimulation(double *temperature, int index, double targetTemp);

    double *getBottomTemperature(); // used for LCD to get the temperature to display
    double *getHotlidTemperature(); // used for LCD to get the temperature to display

    void setpid1startpreHeat80(); // button set this to start pid1 process
    void StartPreheat80();
    void Heat1Preheat80(); // pre heat the heat block 1 to 80
    void pid1Maintain80(); // maintain heat1 to be 80 when heat up hotlid1 and maintain

    void setPreheat67();      // change the status to set epidstartpreHeat67 after button pressing
    void setCalibPreheat55(); // calib flow: enter preheat heater2,3 to 55C
    void calibPreheat55();    // calib flow: heat heater2,3 to 55C, hold 5 min, then maintain
    void calibMaintain55();   // calib flow: hold heater2,3 at 55C during calib/select
    void Heat2_55();
    void Heat3_55();
    void StartPreheat2_67();
    void Preheat2_67();
    void Maintain2_67();

    void StartPreheat3_67();
    void Preheat3_67();
    void Maintain3_67();

    void heatNewLid23();
    void HeatHotlid23();

    void maintainNewLid23();
    void MaintainHotlid23();

    bool getphase2ready();

    void stopAllHeating();
    void stopHeaterBottom(void);
    void stopHeaterTop(void);
};

extern PIDControl _PIDControl;

#endif
