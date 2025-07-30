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
    eSensorpreheat,    // preheat for 15 mins during heater preheating to amp temperature
    eSensormaintain,   // maintain after preheating
    eSensorstart,      // start to initialize the opto
    eSensor1stReading, // start to read the 1st stage opto data

    eSensorcalib // calib
} e_sensorStep;

// struct sensorvalue
// {
//     Word value[10][30] = {0};
// };

class sensor6035
{
private:
    // I2C multiplex channel number to sensor number(0~4)
    uint8_t I2C_Channel[5] = {6, 0, 1, 2, 3}; // by default is old version(PCB V1.2), the sequence of new one is {6, 5, 1, 2, 3};
    TCA9548A I2CMux;                          // Address can be passed into the constructor
    TCA9548A I2CMux1 = TCA9548A(0x74);        // Address can be passed into the constructor

    unsigned long START_INTERVAL_TIME = 0; // this is to record the start time of 1 round/loop reading

    uint8_t COUNTER = 0;
    bool flagCounterDisplay = false; // control to display counter or timeZ
    e_sensorStep sensorStep = eSensorwait;
    uint8_t iChannel = 0; // record the channel that is reading

    Word SENSOR_DATA[7] = {0};
    // float (* calMatrix)[2];// set to _ForteSetting.parameter.slopes later at begin()

    // variables for testing LOD
    int delayTime = 4000;
    int _heater1_PWM = 0;
    int _heater2_PWM = 0;
    int _heater3_PWM = 0;

    int _top_heater1_PWM = 0;
    int _top_heater2_PWM = 0;
    int _top_heater3_PWM = 0;

    bool bSensorReadingFlag = false; // flag to inform heater to on/off hotlid23 heating, enable only when the sensor is reading, disable when it's not reading.
    unsigned long tic;

public:
    Word sensor67Value[10][130] = {{1, 2, 3, 4, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40},  /* Slot 1*/
                                   {1, 2, 3, 4, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40},  /* Slot 2*/
                                   {1, 2, 3, 4, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40},  /* Slot 3*/
                                   {1, 2, 3, 4, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40},  /* Slot 4*/
                                   {1, 2, 3, 4, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40},  /* Slot 5*/
                                   {1, 2, 3, 4, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40},  /* Slot 6*/
                                   {1, 2, 3, 4, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40},  /* Slot 7*/
                                   {1, 2, 3, 4, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40},  /* Slot 8*/
                                   {1, 2, 3, 4, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40},  /* Slot 9*/
                                   {1, 2, 3, 4, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40}}; /* Slot 10*/

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

    void connectToSensor(int slot);

    void disconnectFromSensor(int slot);

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

    bool bResultGet(float *CT_value, char *result);
    bool bResultPutToGoogleSheet(float *CT_value,
                                             char *result,
                                             struct DiagnosticOutcome *get_outcome,
                                             struct FeatureDetection *get_peak_features);
    bool bResultPutToChart(float *CT_value, char *result, float **processced_data);

    void AlgLoop(char *recvData);

    void eSensorParaIni();
    bool getSensorPreheatReady();

    bool JsonDataSplit();
    void setCounterDisplayflag(bool flag);

    void outputHeader();
    float calCalibratedValue(int channel, int cnt);

private:
    void Basic_Initialization_Auto_Mode();
    void Auto_Mode();
    void Power_Saving_Mode();
    void setI2CChannelSeq();
    void ResetAllSensors();
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

    void switchAllSensorsAcquisitionState(bool state);
    bool checkSensorConfiguration();
    void reConfigSingleSensor(int slot);
    void switchSensorAcquisitionState(bool state);

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