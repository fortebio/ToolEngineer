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
    epid1hotlid,         // activate the hotlid
    epid1ready,          // lysis is ready
    // epid1maintain80,    //maintain heat1 to 80 for 10mins, and check the hotlid temperature and make sure it's won't overheat
    epid1finish,         // time out for 10mins, wait for next step
    epid2startpreHeat67, // check before preheat heater2 to 67
    epid2preHeat67,      // preheat heater2 to 67
    epid3startpreHeat67, // preheat heater3 to 67
    epid3preHeat67,      // preheat heater3 to 67
    ehotlid23heat,       // heat hotlid2
    epid23ready          //,        //heater2 and heater3 are ready, wait for next step
    // epidfinish          //finish the testing, result review or restart
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

    // safety check variable
    unsigned long bottomSensorRespTime = 0; // timer to record when the bottom temperature sensor should response the value
    unsigned long topSensorRespTime = 0;    // timer to record when the top temperature sensor should response the value

    unsigned long START_INTERVAL_TIME = 0;
    int COUNTER = 0;

    bool flagheat80Setting = false;
    uint8_t foverHeat = 0;                 // if it's over heat, then need to cool down. e.g. 80->67. This is set to check normal process, not for over heat error
    bool foverHeatErr = false;             // RFU
    unsigned long START_OverHeat_TIME = 0; // RFU

    // flag to control the simulation enable or disable
    bool bheater1Simu = false;
    bool bheater2Simu = false;
    bool bheater3Simu = false;
    // bool bhotlid1Simu = false;
    bool bhotlid23Simu = false;

    bool btemperatureOut = false;

public:
    PIDControl(/* args */);
    ~PIDControl();
    void begin();
    void loop();
    void rerun();
    void rerunPIDBottom(); // rerun the PID for bottom heater
    void rerunPIDTop();    // rerun the PID for top heater

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
    // void HeatHotlid1();             //heat the hotlid 1 to 80
    void pid1Maintain80(); // maintain heat1 to be 80 when heat up hotlid1 and maintain
    // void MaintainHotlid1();

    void setPreheat67(); // change the status to set epidstartpreHeat67 after button pressing
    void StartPreheat2_67();
    void Preheat2_67();
    void Maintain2_67();

    void StartPreheat3_67();
    void Preheat3_67();
    void Maintain3_67();

    void heatOldLid23();
    void heatNewLid23();
    void HeatHotlid23();

    void maintainOldLid23();
    void maintainNewLid23();
    void MaintainHotlid23();

    bool getphase2ready();

    // void setepidfinish();

    void StopHeating();

    void stopAllHeating();
    void stopHeaterBottom(void);
    void stopHeaterTop(void);
};

extern PIDControl _PIDControl;

#endif
