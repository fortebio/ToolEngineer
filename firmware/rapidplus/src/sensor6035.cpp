#include "sensor6035.h"
#include "LED.h"
#include "define.h"
#include "PIDControl.h"
#include "Bluetooth.h"
// #include "ArduinoJson.h"

sensor6035::sensor6035(/* args */)
{
}

sensor6035::~sensor6035()
{
}

void sensor6035::begin()
{
    _LED.begin(); // initialize the LED together with sensor
    // calMatrix = _ForteSetting.parameter.slopes;
    I2CMux.begin(Wire);  // Wire instance is passed to the library
    I2CMux.closeAll();   // Set a base state which we know (also the default state on power on)
    I2CMux1.begin(Wire); // Wire instance is passed to the library
    I2CMux1.closeAll();  // Set a base state which we know (also the default state on power on)

    setI2CChannelSeq();

    ResetAllSensors(); // reset all 10 sensors

    // set all 10 sensors, the configuration can be updated in define.h
    ChannelEnableProcess_loop();
    ALS_IT_Process_loop();
    GainProcess_loop();
    DigitalGainProcess_loop();
    SENS_loop();
    Snapshot_loop_test(); // read one loop at the start

    COUNTER = 0;
    sensorStep = eSensorpreheat;
    _LED.LED_on(0); // turn on the first LED
    iChannel = 0;
    sensor67ValueTime = millis() + LED_DELAY_TIME; // initialize the timer
    START_INTERVAL_TIME = millis();                // as start preheat immediatly, need to record the time together
}

void sensor6035::loop()
{
    switch (sensorStep)
    {
    case eSensorwait: // if waiting, do nothing
        break;
    case eSensorpreheat: // if preheat, then LED and sensor need work for 15mins
        eSensorPreheat();
        break;
    case eSensormaintain:
        eSensorMaintain();
        break;
    case eSensorstart:
        /* code */
        eSensorstartFunc();
        break;

    case eSensor1stReading:
        eSensor1stReadingFunc();
        break;

    case eSensorcalib:
        calibration(_displayCLD.slot);
        break;

    default:
        break;
    }

    // If no received command, exit the loop
    return;
}

void sensor6035::skip2Maintain()
{
    if (sensorStep < eSensormaintain)
    {
        sensorStep = eSensormaintain; // preheat for 15mins already, enter maintain mode
        _displayCLD.bheadershow = true;
        _displayCLD.changeScreen = true;
    }
}

void sensor6035::rerun()
{
    if (sensorStep > eSensorpreheat) // if opto has preheat, then just maintain it
    {
        // info_displayln("sensor only need to maintain status");
        _LED.LED_OFF_ALL();
        clear();
        sensorStep = eSensormaintain;
    }
}

void sensor6035::setCounterDisplayflag(bool flag)
{
    flagCounterDisplay = flag;
}

void sensor6035::setStepeSensorpreheat()
{
    if (sensorStep == eSensorwait)
    {
        sensorStep = eSensorpreheat;
        // info_displayln("setpreheat");
        eSensorParaIni();
    }
}

void sensor6035::setStepeSensorstart()
{
    sensorStep = eSensorstart;
}

#define BREAKING_START_INDEX 5
#define RISING_WINDOW 6
bool sensor6035::bResultGet(float *CT_value, char *result)
{
    // Define a vector of integers
    DataIn recordIn = DataIn();
    Record recordOut = Record();

    char JsonData[3 * 1024] = {0};
    {
        info_displayln("load algo para from flash");
        strcpy(JsonData, strJson.c_str());
    }

    JsonDocument jsonDocument;

    // Deserialize the JSON
    DeserializationError error = deserializeJson(jsonDocument, JsonData);

    // Check for errors in parsing the JSON
    if (error)
    {
        info_display("deserializeJson() failed: ");
        info_displayln(error.c_str());
        return false;
    }
    // map data to record
    recordIn.fromEEPROM(jsonDocument); // get parameter from EEPROM, the rest from json data
    uint8_t loops = _ForteSetting.parameter.amplification_time;
    recordIn.raw_data.resize(loops);
    recordIn.time_data.resize(loops);
    for (size_t i = 0; i < loops; i++)
    {
        recordIn.time_data[i] = float(i) * OPTO_INTERVAL / 60000.0; // send time;
    }

    for (size_t i = 0; i < 10; i++)
    {
        // Update the raw data
        for (size_t j = 0; j < loops; j++)
        {
            recordIn.raw_data[j] = (float(sensor67Value[i][j]) - FORTE_ORIGINS[i]) / FORTE_SLOPES[i];
        }

        /* ----------------------------- */
        size_t breakIndex = check_breakData(recordIn.raw_data, recordIn.parameters.min_increase, BREAKING_START_INDEX);
        size_t risingIndex = check_risingData(recordIn.raw_data, recordIn.parameters.detection_margin_time * (60000 / OPTO_INTERVAL), RISING_WINDOW);

        // --------------------------------------------------
        auto timeBegin = recordIn.time_data.begin();
        auto timeEnd = recordIn.time_data.end();
        auto rawBegin = recordIn.raw_data.begin();
        auto rawEnd = recordIn.raw_data.end();

        bool validForDetection = true;

        if (breakIndex)
        {
            if (risingIndex)
            {
                if (breakIndex > risingIndex)
                {
                    timeEnd = recordIn.time_data.begin() + breakIndex;
                    rawEnd = recordIn.raw_data.begin() + breakIndex;
                }
                else
                {
                    timeBegin = recordIn.time_data.begin() + breakIndex;
                    rawBegin = recordIn.raw_data.begin() + breakIndex;
                }
            }
            else
            {
                validForDetection = false;
            }
        }

        // --------------------------------------------------
        if (validForDetection)
        {
            recordOut.time_data.assign(timeBegin, timeEnd);
            recordOut.raw_data.assign(rawBegin, rawEnd);

            post_process_curve(recordOut,
                               recordIn.parameters.baseline_start,
                               recordIn.parameters.baseline_range,
                               recordIn.parameters.sg_window,
                               recordIn.parameters.sg_order);

            differentiate(recordOut.time_data,
                          recordOut.processed_data,
                          recordOut.differential_data);

            find_sigmoidal_feature(recordOut, recordIn.parameters);
            predict_outcome(recordOut, recordIn.parameters);
        }
        else
        {
            recordOut.peak_features.clear();
            recordOut.outcome.transition_time.clear();
            recordOut.outcome.plateau_point.clear();
            strcpy(recordOut.outcome.outcome, "Break");
        }

        // --------------------------------------------------
        // Luôn post-process toàn bộ dữ liệu gốc (giữ logic cũ)
        // --------------------------------------------------
        recordOut.time_data.assign(recordIn.time_data.begin(), recordIn.time_data.end());
        recordOut.raw_data.assign(recordIn.raw_data.begin(), recordIn.raw_data.end());
        post_process_curve(recordOut,
                           recordIn.parameters.baseline_start,
                           recordIn.parameters.baseline_range,
                           recordIn.parameters.sg_window,
                           recordIn.parameters.sg_order);

        differentiate(recordOut.time_data,
                      recordOut.processed_data,
                      recordOut.differential_data);

        // re-write data processing to look god for users without affecting performance of algorithm
        /*
        Josep @ 24/12/24: high level of smoothing is bad for finding the lag phase because the smoothing tends to create a smooth transition until t = 0
        Thus, small windows and ordders for smoothing produced best results so far.
        At the same time, may look awesome for display.
        Consider re-running the smoothing at this stage (line commented below) with higher order and window size to make a nice display without affecting the algorithm performance
        */
        Serial.printf("Slot %d:\n", i + 1);
        Serial.printf("Outcome check: %s\n", recordOut.outcome.outcome);
        Serial.printf("Index Rising data : %f\n", (double)risingIndex / 3);
        Serial.printf("Index Break data : %f\n", (double)breakIndex / 3);
        Serial.printf("Index Transition time: %f\n", (double)recordOut.outcome.transition_time.i / 3);
        JsonDocument jsonOut = recordOut.toJSON();
        delay(100);
        serializeJson(jsonOut, Serial);
        info_displayln();
        info_displayln();

        CT_value[i] = float(recordOut.outcome.transition_time.x);
        result[i] = recordOut.outcome.outcome[0];

        // reset records
        recordOut.clear();
    }

    return true;
}

bool sensor6035::bResultPutToChart(float *CT_value, char *result, float **processed_data)
{
    DataIn recordIn = DataIn();
    Record recordOut = Record();

    char JsonData[3 * 1024] = {0};

    strcpy(JsonData, strJson.c_str());

    JsonDocument jsonDocument;
    // Deserialize the JSON
    DeserializationError error = deserializeJson(jsonDocument, JsonData);

    // Check for errors in parsing the JSON
    if (error)
    {
        info_display("deserializeJson() failed: ");
        info_displayln(error.c_str());
        return false;
    }
    // map data to record
    recordIn.fromEEPROM(jsonDocument); // get parameter from EEPROM, the rest from json data
    uint8_t loops = _ForteSetting.parameter.amplification_time;
    recordIn.raw_data.resize(loops);
    recordIn.time_data.resize(loops);

    for (size_t i = 0; i < loops; i++)
    {
        recordIn.time_data[i] = float(i) * OPTO_INTERVAL / 60000.0; // send time;
    }

    for (size_t i = 0; i < 10; i++)
    {
        // Update the raw data
        for (size_t j = 0; j < loops; j++)
        {
            recordIn.raw_data[j] = (float(sensor67Value[i][j]) - FORTE_ORIGINS[i]) / FORTE_SLOPES[i];
        }

        // deep copy fluorescence data to record object
        recordOut.time_data.assign(recordIn.time_data.begin(), recordIn.time_data.end());
        recordOut.raw_data.assign(recordIn.raw_data.begin(), recordIn.raw_data.end());

        // process data
        post_process_curve(recordOut,
                           recordIn.parameters.baseline_start,
                           recordIn.parameters.baseline_range,
                           recordIn.parameters.sg_window,
                           recordIn.parameters.sg_order);
        // differentiate
        differentiate(recordOut.time_data, recordOut.processed_data, recordOut.differential_data);

        // detect feature
        find_sigmoidal_feature(recordOut, recordIn.parameters);

        // detect amplification
        predict_outcome(recordOut, recordIn.parameters);
        if ((recordOut.outcome.outcome[0] == 'P') || (recordOut.outcome.outcome[0] == 'S') || (recordOut.outcome.outcome[0] == 'E'))
        {
            if (!check_breakData(recordOut.raw_data, recordOut.outcome.transition_time.x, recordOut.outcome.transition_time.i))
            {
                strcpy(recordOut.outcome.outcome, "Break");
            }
        }

        // re-write data processing to look god for users without affecting performance of algorithm
        /*
        Josep @ 24/12/24: high level of smoothing is bad for finding the lag phase because the smoothing tends to create a smooth transition until t = 0
        Thus, small windows and ordders for smoothing produced best results so far.
        At the same time, may look awesome for display.
        Consider re-running the smoothing at this stage (line commented below) with higher order and window size to make a nice display without affecting the algorithm performance
        */

        processed_data[i] = (float *)malloc(sizeof(float) * loops);
        if (processed_data[i] == NULL)
        {
            info_displayln("Memory alloccation failed for processed_data");
            return false;
        }
        for (uint8_t j = 0; j < loops; j++)
        {
            processed_data[i][j] = (float)recordOut.processed_data[j];
        }

        CT_value[i] = float(recordOut.outcome.transition_time.x);
        result[i] = recordOut.outcome.outcome[0];

        // reset records
        recordOut.clear();
    }
    return true;
}

bool sensor6035::bResultPutToGoogleSheet(float *CT_value,
                                         char *result,
                                         struct DiagnosticOutcome *get_outcome,
                                         struct FeatureDetection *get_peak_features)
{
    DataIn recordIn = DataIn();
    Record recordOut = Record();
    char JsonData[3 * 1024] = {0};
    JsonDocument jsonDocument;

    strcpy(JsonData, strJson.c_str());

    // Deserialize the JSON
    DeserializationError error = deserializeJson(jsonDocument, JsonData);

    // Check for errors in parsing the JSON
    if (error)
    {
        info_display("deserializeJson() failed: ");
        info_displayln(error.c_str());
        return false;
    }
    // map data to record
    recordIn.fromEEPROM(jsonDocument); // get parameter from EEPROM, the rest from json data
    uint8_t loops = _ForteSetting.parameter.amplification_time;
    recordIn.raw_data.resize(loops);
    recordIn.time_data.resize(loops);
    for (size_t i = 0; i < loops; i++)
    {
        recordIn.time_data[i] = float(i) * OPTO_INTERVAL / 60000.0; // send time;
    }

    for (size_t i = 0; i < 10; i++)
    {
        // Update the raw data
        for (size_t j = 0; j < loops; j++)
        {
            recordIn.raw_data[j] = (float(sensor67Value[i][j]) - FORTE_ORIGINS[i]) / FORTE_SLOPES[i];
        }

        /* ----------------------------- */
        size_t breakIndex = check_breakData(recordIn.raw_data, recordIn.parameters.min_increase, BREAKING_START_INDEX);
        size_t risingIndex = check_risingData(recordIn.raw_data, recordIn.parameters.detection_margin_time * (60000 / OPTO_INTERVAL), RISING_WINDOW);

        // --------------------------------------------------
        auto timeBegin = recordIn.time_data.begin();
        auto timeEnd = recordIn.time_data.end();
        auto rawBegin = recordIn.raw_data.begin();
        auto rawEnd = recordIn.raw_data.end();

        bool validForDetection = true;

        if (breakIndex)
        {
            if (risingIndex)
            {
                if (breakIndex > risingIndex)
                {
                    timeEnd = recordIn.time_data.begin() + breakIndex;
                    rawEnd = recordIn.raw_data.begin() + breakIndex;
                }
                else
                {
                    timeBegin = recordIn.time_data.begin() + breakIndex;
                    rawBegin = recordIn.raw_data.begin() + breakIndex;
                }
            }
            else
            {
                validForDetection = false;
            }
        }

        // --------------------------------------------------
        if (validForDetection)
        {
            recordOut.time_data.assign(timeBegin, timeEnd);
            recordOut.raw_data.assign(rawBegin, rawEnd);

            post_process_curve(recordOut,
                               recordIn.parameters.baseline_start,
                               recordIn.parameters.baseline_range,
                               recordIn.parameters.sg_window,
                               recordIn.parameters.sg_order);

            differentiate(recordOut.time_data,
                          recordOut.processed_data,
                          recordOut.differential_data);

            find_sigmoidal_feature(recordOut, recordIn.parameters);
            predict_outcome(recordOut, recordIn.parameters);
        }
        else
        {
            recordOut.peak_features.clear();
            recordOut.outcome.transition_time.clear();
            recordOut.outcome.plateau_point.clear();
            strcpy(recordOut.outcome.outcome, "Break");
        }

        // --------------------------------------------------
        // Luôn post-process toàn bộ dữ liệu gốc (giữ logic cũ)
        // --------------------------------------------------
        recordOut.time_data.assign(recordIn.time_data.begin(), recordIn.time_data.end());
        recordOut.raw_data.assign(recordIn.raw_data.begin(), recordIn.raw_data.end());
        post_process_curve(recordOut,
                           recordIn.parameters.baseline_start,
                           recordIn.parameters.baseline_range,
                           recordIn.parameters.sg_window,
                           recordIn.parameters.sg_order);

        differentiate(recordOut.time_data,
                      recordOut.processed_data,
                      recordOut.differential_data);

        // re-write data processing to look god for users without affecting performance of algorithm
        /*
        Josep @ 24/12/24: high level of smoothing is bad for finding the lag phase because the smoothing tends to create a smooth transition until t = 0
        Thus, small windows and ordders for smoothing produced best results so far.
        At the same time, may look awesome for display.
        Consider re-running the smoothing at this stage (line commented below) with higher order and window size to make a nice display without affecting the algorithm performance
        */
        Serial.printf("Slot %d:\n", i + 1);
        Serial.printf("Outcome check: %s\n", recordOut.outcome.outcome);
        Serial.printf("Index Rising data : %f\n", (double)risingIndex / 3);
        Serial.printf("Index Break data : %f\n", (double)breakIndex / 3);
        Serial.printf("Index Transition time: %f\n", (double)recordOut.outcome.transition_time.i / 3);
        JsonDocument jsonOut = recordOut.toJSON();
        delay(100);
        serializeJson(jsonOut, Serial);
        info_displayln();
        info_displayln();

        get_outcome[i] = recordOut.outcome;
        get_peak_features[i] = recordOut.peak_features;
        CT_value[i] = float(recordOut.outcome.transition_time.x);
        result[i] = recordOut.outcome.outcome[0];
        recordOut.clear();
    }

    return true;
}

void sensor6035::AlgLoop(char *recvData)
{
    DataIn recordIn = DataIn();
    Record recordOut = Record();
    JsonDocument jsonDocument;
    // Deserialize the JSON
    DeserializationError error = deserializeJson(jsonDocument, recvData);

    // Check for errors in parsing the JSON
    if (error)
    {
        Serial.print("deserializeJson() failed: ");
        Serial.println(error.c_str());
        return;
    }
    // map data to record
    recordIn.fromEEPROM(jsonDocument);

    // deep copy fluorescence data to record object
    recordOut.time_data.assign(recordIn.time_data.begin(), recordIn.time_data.end());
    recordOut.raw_data.assign(recordIn.raw_data.begin(), recordIn.raw_data.end());

    // process data
    post_process_curve(recordOut, recordIn.parameters.baseline_start, recordIn.parameters.baseline_range, recordIn.parameters.sg_window, recordIn.parameters.sg_order);

    // differntiatie
    differentiate(recordOut.time_data, recordOut.processed_data, recordOut.differential_data);

    // detect feature
    find_sigmoidal_feature(recordOut, recordIn.parameters);

    // detect amplification
    predict_outcome(recordOut, recordIn.parameters);

    /*
    Josep @ 24/12/24: high level of smoothing is bad for finding the lag phase because the smoothing tends to create a smooth transition until t = 0
    Thus, small windows and ordders for smoothing produced best results so far.
    At the same time, may look awesome for display.
    Consider re-running the smoothing at this stage (line commented below) with higher order and window size to make a nice display without affecting the algorithm performance
    */
    // post_process_curve(recordOut, recordIn.parameters.baseline_start, recordIn.parameters.baseline_range, recordIn.parameters.sg_window, recordIn.parameters.sg_order);
    JsonDocument jsonOut = recordOut.toJSON();

    // Print the parsed JSON dictionary
    serializeJson(jsonOut, Serial);
    Serial.println();

    // reset records
    recordIn.clear();
    recordOut.clear();
}

void sensor6035::eSensorParaIni()
{
    // START_INTERVAL_TIME = 0;
    COUNTER = 0;
    iChannel = 0;
    if (sensor67ValueTime == 0) // only for the first time when sensor67ValueTime is not initialized yet
    {
        _LED.LED_on(0);                                // turn on the first LED
        sensor67ValueTime = millis() + LED_DELAY_TIME; // initialize the timer
        info_display("Set sensor67ValueTime when it's zero\n");
    }
    START_INTERVAL_TIME = millis(); // as start preheat immediatly, need to record the time together
}

bool sensor6035::getSensorPreheatReady()
{
    // info_displayf("sensor step %d:%d\n", sensorStep, eSensormaintain)
    return sensorStep == eSensormaintain;
}

// Auto/Self-Timed Mode Basic Initialization Function
void sensor6035::Basic_Initialization_Auto_Mode()
{
    // 1.) Enable ALS Channel only (Disable White channel)
    VEML6035_SET_CHANNEL_EN(VEML6035_WHITE_CH_DIS);
    // VEML6035_SET_CHANNEL_EN(VEML6035_WHITE_CH_EN);

    // 2.) Switch On the ALS Sensor
    VEML6035_SET_SD(VEML6035_ALS_SD_ON);
    delay(100);
}

// Auto/Self-Timed Mode Initialization Function
void sensor6035::Auto_Mode()
{
    // 1.) Initialization
    // Switch Off the ALS Sensor
    //  VEML6035_SET_SD(VEML6035_ALS_SD_ON);
    VEML6035_SET_SD(VEML6035_ALS_SD_OFF);

    // 2.) Setting up ALS/White Channel
    // ALS_CONF0
    // Set the Sensitivity
    VEML6035_SET_SENS(VEML6035_SENS_0_x1);

    // Set the Digital Gain (DG)
    VEML6035_SET_DG(VEML6035_DG_1_DOUBLE);

    // Set the Gain
    VEML6035_SET_GAIN(VEML6035_GAIN_1_DOUBLE);

    // Set the Integration Time
    VEML6035_SET_ALS_IT(VEML6035_ALS_IT_100ms);

    // Set the Persistence
    VEML6035_SET_ALS_PERS(VEML6035_ALS_PERS_1);

    // Set the Interrupt Channel
    VEML6035_SET_INT_CHANNEL(VEML6035_ALS_CH_INT_EN);

    // Enable/Disable White Channel
    VEML6035_SET_CHANNEL_EN(VEML6035_WHITE_CH_DIS);

    // Enable/Disable Interrupt
    VEML6035_SET_INT_EN(VEML6035_ALS_INT_EN);

    // ALS_WH
    // Set the ALS/White Interrupt Higher Threshold
    VEML6035_SET_ALS_HighThreshold(10000);

    // ALS_WL
    // Set the ALS/White Interrupt Lower Threshold
    VEML6035_SET_ALS_LowThreshold(8000);

    // Power Saving Mode
    // Disable the Power Saving Mode
    VEML6035_SET_PSM_EN(VEML6035_ALS_PSM_DIS);

    // 3.) Switch On the ALS Sensor
    VEML6035_SET_SD(VEML6035_ALS_SD_ON);

    // Clear Initial Interrupt
    VEML6035_GET_IF();

    delay(300);
}

// Power Saving Mode Initialization Function
void sensor6035::Power_Saving_Mode()
{
    // 1.) Initialization
    // Switch Off the ALS Sensor
    VEML6035_SET_SD(VEML6035_ALS_SD_ON);

    // 2.) Setting up ALS/White Channel
    // ALS_CONF0
    // Set the Sensitivity
    VEML6035_SET_SENS(VEML6035_SENS_0_x1);

    // Set the Digital Gain (DG)
    VEML6035_SET_DG(VEML6035_DG_1_DOUBLE);

    // Set the Gain
    VEML6035_SET_GAIN(VEML6035_GAIN_1_DOUBLE);

    // Set the Integration Time
    VEML6035_SET_ALS_IT(VEML6035_ALS_IT_100ms);

    // Set the Persistence
    VEML6035_SET_ALS_PERS(VEML6035_ALS_PERS_1);

    // Set the Interrupt Channel
    VEML6035_SET_INT_CHANNEL(VEML6035_ALS_CH_INT_EN);

    // Enable/Disable White Channel
    VEML6035_SET_CHANNEL_EN(VEML6035_WHITE_CH_DIS);

    // Enable/Disable Interrupt
    VEML6035_SET_INT_EN(VEML6035_ALS_INT_EN);

    // ALS_WH
    // Set the ALS/White Interrupt Higher Threshold
    VEML6035_SET_ALS_HighThreshold(10000);

    // ALS_WL
    // Set the ALS/White Interrupt Lower Threshold
    VEML6035_SET_ALS_LowThreshold(8000);

    // Power Saving Mode
    // Disable the Power Saving Mode
    VEML6035_SET_PSM_EN(VEML6035_ALS_PSM_EN);

    // Set Power Saving Mode Waiting Time
    VEML6035_SET_PSM_WAIT(VEML6035_ALS_PSM_WAIT_3_2);

    // 3.) Switch On the ALS Sensor
    VEML6035_SET_SD(VEML6035_ALS_SD_ON);

    // Clear Initial Interrupt
    VEML6035_GET_IF();

    delay(1000);
}

void sensor6035::setI2CChannelSeq()
{
    if (strcmp(_ForteSetting.parameter.PCB_version, "V1.3") < 0)
    {
        uint8_t I2C_ChannelV12[5] = {6, 0, 1, 2, 3};
        memcpy(I2C_Channel, I2C_ChannelV12, 5);
        info_displayln("sensor: V1.2 setting");
    }
    else
    {
        uint8_t I2C_ChannelV13[5] = {6, 5, 1, 2, 3};
        memcpy(I2C_Channel, I2C_ChannelV13, 5);
        info_displayln("sensor: V1.3 setting");
    }

    info_display("set the seq to be: ");
    for (size_t i = 0; i < 5; i++)
    {
        info_displayf("%d ", I2C_Channel[i]);
    }
    info_displayln("");
}

void sensor6035::ResetAllSensors()
{
    // reset the 1st 5 sensors
    for (int iChannel = 0; iChannel < 5; iChannel++)
    {
        I2CMux.openChannel(I2C_Channel[iChannel]);
        /* Reset Sensor to default value */
        Reset_Sensor();
        Basic_Initialization_Auto_Mode();
        I2CMux.closeChannel(I2C_Channel[iChannel]);
    }
    // reset the 2nd 5 sensors
    for (int iChannel = 0; iChannel < 5; iChannel++)
    {
        I2CMux1.openChannel(I2C_Channel[iChannel]);
        /* Reset Sensor to default value */
        Reset_Sensor();
        Basic_Initialization_Auto_Mode();
        I2CMux1.closeChannel(I2C_Channel[iChannel]);
    }
}

void sensor6035::ChannelEnableProcess_loop()
{
    // process the 1st 5 sensors
    for (int iChannel = 0; iChannel < 5; iChannel++)
    {
        I2CMux.openChannel(I2C_Channel[iChannel]);
        info_display("Sensor ");
        info_display(iChannel + 1);
        info_display(": ");
        ChannelEnableProcess(ChannelEnableRead); // read the initial value
        ChannelEnableProcess(ChannelEnableSet);  // set the required value and read again
        I2CMux.closeChannel(I2C_Channel[iChannel]);
    }
    // process the 2nd 5 sensors
    for (int iChannel = 0; iChannel < 5; iChannel++)
    {
        I2CMux1.openChannel(I2C_Channel[iChannel]);
        info_display("Sensor ");
        info_display(iChannel + 6);
        info_display(": ");
        ChannelEnableProcess(ChannelEnableRead); // read the initial value
        ChannelEnableProcess(ChannelEnableSet);  // set the required value and read again
        I2CMux1.closeChannel(I2C_Channel[iChannel]);
    }
    info_displayln("////////////////");
}

// Function to process CHANNEL_EN, ALS only or ALS&White
// Command List:
// CHANNEL_EN: Read channel para
// CHANNEL_EN ALS: Set channel to read ALS only
// CHANNEL_EN Both: Set channel to read ALS&White
void sensor6035::ChannelEnableProcess(String command)
{
    if (command == ChannelEnableRead) // read para
    {
        /* read para and response, add later */
        info_displayf("Check Channel Enable(CHANNEL_EN): %s\n", VEML6035_GET_CHANNEL_EN_Bit() ? "ALS&White" : "ALS Only"); // Channel enable function: 0 = ALS CH enable only, 1 = ALS and WHITE CH enable
        return;
    }
    else if (command == ChannelEnableSetALS)
    {
        VEML6035_SET_CHANNEL_EN(VEML6035_WHITE_CH_DIS);
    }
    else if (command == ChannelEnableSetBoth)
    {
        VEML6035_SET_CHANNEL_EN(VEML6035_WHITE_CH_EN);
    }
    else
    {
        info_displayln("Invalid command");
    }
    info_displayf("Set Channel Enable(CHANNEL_EN) to %s\n", VEML6035_GET_CHANNEL_EN_Bit() ? "ALS&White" : "ALS Only"); // Channel enable function: 0 = ALS CH enable only, 1 = ALS and WHITE CH enable
}

void sensor6035::ALS_IT_Process_loop()
{
    // process the 1st 5 sensors
    for (int iChannel = 0; iChannel < 5; iChannel++)
    {
        I2CMux.openChannel(I2C_Channel[iChannel]);
        info_display("Sensor ");
        info_display(iChannel + 1);
        info_display(": ");
        ALS_IT_Process(ALSITRead); // read the initial value
        ALS_IT_Process(ALSITSet);  // set the required value and read again
        I2CMux.closeChannel(I2C_Channel[iChannel]);
    }
    // process the 2nd 5 sensors
    for (int iChannel = 0; iChannel < 5; iChannel++)
    {
        I2CMux1.openChannel(I2C_Channel[iChannel]);
        info_display("Sensor ");
        info_display(iChannel + 6);
        info_display(": ");
        ALS_IT_Process(ALSITRead); // read the initial value
        ALS_IT_Process(ALSITSet);  // set the required value and read again
        I2CMux1.closeChannel(I2C_Channel[iChannel]);
    }
    info_displayln("////////////////");
}

// Function to process ALS integration time
// Command List:
// ALS_IT: Read ALS IT para
// ALS_IT 25: Set ALS IT to 25ms
// ALS_IT 50: Set ALS IT to 50ms
// ALS_IT 100: Set ALS IT to 100ms
// ALS_IT 200: Set ALS IT to 200ms
// ALS_IT 400: Set ALS IT to 400ms
// ALS_IT 800: Set ALS IT to 800ms
void sensor6035::ALS_IT_Process(String command)
{
    const String strParaList[6] = {"25", "50", "100", "200", "400", "800"};

    if (command == ALSITRead) // read para
    {
        Byte readValue = VEML6035_GET_ALS_IT_Bits();
        if (readValue < 1 || readValue > 6)
        {
            info_displayf("Returned ALS IT value %d is wrong\n", readValue);
            return;
        }
        /* read para and response, add later */
        info_displayf("Read ALS integration time(ALS_IT): %s\n", strParaList[readValue - 1]); // 25, 50, 100, 200, 400, 800
        return;
    }
    else if (command.substring(0, 7) == "ALS_IT ")
    {
        const Word paraList[6] = {VEML6035_ALS_IT_25ms, VEML6035_ALS_IT_50ms, VEML6035_ALS_IT_100ms, VEML6035_ALS_IT_200ms, VEML6035_ALS_IT_400ms, VEML6035_ALS_IT_800ms};
        String strPara = command.substring(7);
        int index = strPara.toInt();
        VEML6035_SET_ALS_IT(paraList[index]); // set the para here
        Byte readValue = VEML6035_GET_ALS_IT_Bits();
        if (readValue < 1 || readValue > 6)
        {
            info_displayf("Returned ALS IT value %d is wrong\n", readValue);
            return;
        }
        /* read para and response, add later */
        info_displayf("Set ALS integration time(ALS_IT) to %s\n", strParaList[readValue - 1]); // 25, 50, 100, 200, 400, 800
    }
    else
    {
        info_displayln("Invalid command");
    }
}

void sensor6035::GainProcess_loop()
{
    // process the 1st 5 sensors
    for (int iChannel = 0; iChannel < 5; iChannel++)
    {
        I2CMux.openChannel(I2C_Channel[iChannel]);
        info_display("Sensor ");
        info_display(iChannel + 1);
        info_display(": ");
        GainProcess(GAINRead); // read the initial value
        GainProcess(GAINSet);  // set the required value and read again
        I2CMux.closeChannel(I2C_Channel[iChannel]);
    }
    // process the 2nd 5 sensors
    for (int iChannel = 0; iChannel < 5; iChannel++)
    {
        I2CMux1.openChannel(I2C_Channel[iChannel]);
        info_display("Sensor ");
        info_display(iChannel + 6);
        info_display(": ");
        GainProcess(GAINRead); // read the initial value
        GainProcess(GAINSet);  // set the required value and read again
        I2CMux1.closeChannel(I2C_Channel[iChannel]);
    }
    info_displayln("////////////////");
}

// Function to process gain
// Command List:
// GAIN: Read GAIN para
// GAIN Normal: Set GAIN to Normal
// GAIN Double: Set GAIN to Double
void sensor6035::GainProcess(String command)
{
    if (command == GAINRead) // read para
    {
        /* read para and response, add later */
        info_displayf("Read Gain(GAIN): %s\n", VEML6035_GET_GAIN() ? "Double" : "Normal"); // 0 = normal, 1 = double
        return;
    }
    else if (command.substring(0, 5) == "GAIN ")
    {
        /* set the para and response OK */
        if (command.indexOf(" Normal", 3) != -1) // GAIN N(ormal)
        {
            VEML6035_SET_GAIN(VEML6035_GAIN_0_NORMAL);
        }
        else if (command.indexOf(" Double", 3) != -1) // GAIN D(ouble)
        {
            VEML6035_SET_GAIN(VEML6035_GAIN_1_DOUBLE);
        }
        info_displayf("Set Gain(GAIN) to %s\n", VEML6035_GET_GAIN() ? "Double" : "Normal"); // 0 = normal, 1 = double
    }
    else
    {
        info_displayln("Invalid command");
    }
}

void sensor6035::DigitalGainProcess_loop()
{
    // process the 1st 5 sensors
    for (int iChannel = 0; iChannel < 5; iChannel++)
    {
        I2CMux.openChannel(I2C_Channel[iChannel]);
        info_display("Sensor ");
        info_display(iChannel + 1);
        info_display(": ");
        DigitalGainProcess(DGRead); // read the initial value
        DigitalGainProcess(DGSet);  // set the required value and read again
        I2CMux.closeChannel(I2C_Channel[iChannel]);
    }
    // process the 2nd 5 sensors
    for (int iChannel = 0; iChannel < 5; iChannel++)
    {
        I2CMux1.openChannel(I2C_Channel[iChannel]);
        info_display("Sensor ");
        info_display(iChannel + 6);
        info_display(": ");
        DigitalGainProcess(DGRead); // read the initial value
        DigitalGainProcess(DGSet);  // set the required value and read again
        I2CMux1.closeChannel(I2C_Channel[iChannel]);
    }
    info_displayln("////////////////");
}
float sensor6035::calCalibratedValue(int channel, int cnt)
{
    return (float(sensor67Value[channel][cnt]) - FORTE_ORIGINS[channel]) / FORTE_SLOPES[channel];
}
// Function to process digital gain
// Command List:
// DG: Read DG para
// DG Normal: Set DG to Normal
// DG Double: Set DG to Double
void sensor6035::DigitalGainProcess(String command)
{
    if (command == "DG") // read para
    {
        /* read para and response, add later */
        info_displayf("Read Digital Gain(DG): %s\n", VEML6035_GET_DG() ? "Double" : "Normal"); // 0 = normal, 1 = double
        return;
    }
    else if (command.substring(0, 3) == "DG ")
    {
        /* set the para and response OK */
        if (command.indexOf(" Normal", 1) != -1) // DG N(ormal)
        {
            VEML6035_SET_DG(VEML6035_DG_0_NORMAL);
        }
        else if (command.indexOf(" Double", 1) != -1) // DG D(ouble)
        {
            VEML6035_SET_DG(VEML6035_DG_1_DOUBLE);
        }
        info_displayf("Set Digital Gain(DG) to %s\n", VEML6035_GET_DG() ? "Double" : "Normal"); // 0 = normal, 1 = double
    }
    else
    {
        info_displayln("Invalid command");
    }
}

void sensor6035::SENS_loop()
{
    // process the 1st 5 sensors
    for (int iChannel = 0; iChannel < 5; iChannel++)
    {
        I2CMux.openChannel(I2C_Channel[iChannel]);
        info_display("Sensor ");
        info_display(iChannel + 1);
        info_display(": ");
        SENS(SENSRead); // read the initial value
        SENS(SENSSet);  // set the required value and read again
        I2CMux.closeChannel(I2C_Channel[iChannel]);
    }
    // process the 2nd 5 sensors
    for (int iChannel = 0; iChannel < 5; iChannel++)
    {
        I2CMux1.openChannel(I2C_Channel[iChannel]);
        info_display("Sensor ");
        info_display(iChannel + 6);
        info_display(": ");
        SENS(SENSRead); // read the initial value
        SENS(SENSSet);  // set the required value and read again
        I2CMux1.closeChannel(I2C_Channel[iChannel]);
    }
    info_displayln("////////////////");
}

// Function to process SENS
// Command list:
// SENS: Read SENS para
// SENS High: Set SENS to High
// SENS Low: Set SENS to low
void sensor6035::SENS(String command)
{
    if (command == "SENS") // read para
    {
        /* read para and response, add later */
        info_displayf("Read Sensitivity(SENS): %s\n", VEML6035_GET_SENS() ? "Low" : "High"); // 0 = high sensitivity (1 x), 1 = low sensitivity (1/8 x)
        return;
    }
    else if (command.substring(0, 5) == "SENS ")
    {
        /* set the para and response OK */
        // Set the Sensitivity
        if (command.indexOf(" High", 3) != -1) // SENS H(igh)
        {
            VEML6035_SET_SENS(VEML6035_SENS_0_x1);
        }
        else if (command.indexOf(" Low", 3) != -1) // SENS L(ow)
        {
            VEML6035_SET_SENS(VEML6035_SENS_1_x1_8);
        }
        info_displayf("Set Sensitivity(SENS) to %s\n", VEML6035_GET_SENS() ? "Low" : "High"); // 0 = high sensitivity (1 x), 1 = low sensitivity (1/8 x)
    }
    else
    {
        info_displayln("Invalid command");
    }
}

void sensor6035::Snapshot_loop_test()
{
    // delay(1000);
    // _LED.LED_PWM_Set(LED_PWM_VALUE);        //switch on the LED driver before testing
    for (iChannel = 0; iChannel < 10; iChannel++)
    {
        _LED.LED_on(iChannel);
        delay(800);
        if (iChannel < 5)
        {
            I2CMux.openChannel(I2C_Channel[4 - iChannel]);
        }
        else
        {
            I2CMux1.openChannel(I2C_Channel[4 + 5 - iChannel]);
        }
        info_display("Sensor ");
        info_display(iChannel + 1);
        info_display(": ");
        errCnt = 0;
        Snapshot();
        while (errCnt)
        {
            delay(100);
            Snapshot();
            uint8_t errType = 0xFF;
            if (errCnt > 10)
            {
                info_displayf("\n%dth opto sensor error!!! More details are shown as below:\n", iChannel + 1);
                for (uint8_t i = 0; i < 10; i++)
                {
                    for (uint8_t j = 0; j < 3; j++)
                    {
                        if (errRecord[i][j])
                        {
                            if (i == iChannel)
                            {
                                errType = j;
                            }
                            info_displayf("Opto sensor %d reading err type %d for %d times\n", i + 1, j, errRecord[i][j]);
                            errRecord[i][j] = 0;
                        }
                    }
                }
                switch (errType)
                {
                case 0:
                    /* code */
                    _displayCLD.ErrorProcessatBegin("Opto sensor error\n Please power off/on", String(iChannel + 1));
                    break;
                case 1:
                    _displayCLD.ErrorProcessatBegin("Too dark\n Please check", String(iChannel + 1));
                    break;
                case 2:
                    _displayCLD.ErrorProcessatBegin("Too bright\n Please check", String(iChannel + 1));
                    break;
                default:
                    _displayCLD.ErrorProcessatBegin("Unknown Err\n Please power off/on", String(iChannel + 1));
                    break;
                }
                sleep(3);
                break;
                // errCnt = 1;     //recheck after 3 seconds
            }
        }
        _LED.LED_off(iChannel);
        if (iChannel < 5)
        {
            I2CMux.closeChannel(I2C_Channel[4 - iChannel]);
        }
        else
        {
            I2CMux1.closeChannel(I2C_Channel[4 + 5 - iChannel]);
        }
    }

    // for(iChannel = 5; iChannel < 10; iChannel++)
    // {
    //     _LED.LED_on(iChannel);        // turn on the LED
    //     delay(800);
    //     I2CMux1.openChannel(I2C_Channel[4+5-iChannel]);
    //     info_display("Sensor ");
    //     info_display(iChannel+1);
    //     info_display(": ");
    //     errCnt = 0;
    //     Snapshot();
    //     while (errCnt)
    //     {
    //         Snapshot();
    //         if (errCnt > 10)
    //         {
    //             info_displayf("\n%dth opto sensor error!!! More details are shown as below:\n", iChannel+1);
    //             for (uint8_t i = 5; i < 10; i++)
    //             {
    //                 for (uint8_t j = 0; j < 3; j++)
    //                 {
    //                     if (errRecord[i][j])
    //                     {
    //                         info_displayf("Opto sensor %d reading err type %d for %d times\n", i+1, j, errRecord[i][j]);
    //                         errRecord[i][j] = 0;
    //                     }
    //                 }
    //             }
    //             _displayCLD.ErrorProcessatBegin("Opto sensor error\n Please restart power", String(iChannel+1));
    //             sleep(3);
    //             break;
    //             // errCnt = 1;     //recheck after 3 seconds
    //         }
    //     }
    //     _LED.LED_off(iChannel);       //turn off the LED
    //     I2CMux1.closeChannel(I2C_Channel[4+5-iChannel]);

    // }
    _LED.LED_PWM_Set(0); // Switch off LED driver after the testing
    info_displayln("////////////////");
}
// Function to process Snapshot
// Command list:
// Snapshot: Read ALS and White light output
void sensor6035::Snapshot()
{
    /* read the value and send back */
    Word sensorResp = 0xFFFF; // VEML6035_GET_ALS_DATA();
    bool flagres = VEML6035_GET_ALS_DATA_I2C_Res(&sensorResp);
    // if (!flagres && (sensorResp == 0) && (errCnt > 5))  //if read value 0 for 5 times, then change it to correct one, in case the real reading is zero
    // {
    //     /* code */
    // }

    if (flagres || (sensorResp == 0) || (sensorResp == 0xFFFF)) // if error happened, start the err process
    {
        info_displayf("Reading error. flag: %d; resp: %d\n", flagres, sensorResp);
        errRereadTime = millis() + 100; // try to read again after 100ms
        errCnt++;
        if (flagres)
        {
            errRecord[iChannel][0]++;
        }
        else if (sensorResp == 0)
        {
            errRecord[iChannel][1]++;
        }
        else
        {
            errRecord[iChannel][2]++;
        }

        // if (errCnt > 3)     //if err more than 3 times, then try to initialize it
        // {
        //     Reset_Sensor();
        //     // Basic_Initialization_Auto_Mode();
        //     VEML6035_SET_CHANNEL_EN(VEML6035_WHITE_CH_DIS);
        //     // VEML6035_SET_CHANNEL_EN(VEML6035_WHITE_CH_EN);
        //     //2.) Switch On the ALS Sensor
        //     VEML6035_SET_SD(VEML6035_ALS_SD_ON);
        //     delay(50);
        //     // ChannelEnableProcess(ChannelEnableRead);        //read the initial value
        //     ChannelEnableProcess(ChannelEnableSet);         //set the required value and read again
        //     // ALS_IT_Process(ALSITRead);        //read the initial value
        //     ALS_IT_Process(ALSITSet);         //set the required value and read again
        //     // GainProcess(GAINRead);        //read the initial value
        //     GainProcess(GAINSet);         //set the required value and read again
        //     // DigitalGainProcess(DGRead);        //read the initial value
        //     DigitalGainProcess(DGSet);         //set the required value and read again
        //     // SENS(SENSRead);        //read the initial value
        //     SENS(SENSSet);         //set the required value and read again
        // }

        return;
    }
    // if(flagRes)
    // {
    //     info_displayln("sensor error reading once");
    //     for (uint8_t i = 2; i < 12; i++)
    //     {
    //         delay(100);     //wait 100ms before reading again
    //         flagRes = VEML6035_GET_ALS_DATA_I2C_Res(&value);
    //         if (flagRes)
    //         {
    //             info_displayf("Read failure %d times\n", i);
    //             if (i == 11)
    //             {
    //                 errCnt = 12;
    //                 return;
    //             }

    //         }
    //         else
    //         {
    //             info_displayf("Read success at %dth time\n", i);
    //             break;
    //         }
    //     }
    // }
    errCnt = 0;
    info_display("Single Shot: {Green: ");
    info_display(sensorResp);
    info_display(", Calibrated: ");
    info_display(((float(sensorResp) - FORTE_ORIGINS[iChannel]) / FORTE_SLOPES[iChannel]));
    info_displayln("}");
    return;
}

void sensor6035::eSensorPreheat()
{
    // if((millis() - START_DURATION_TIME) <= OPTO_PREHEAT_DURATION+50*1000)
    if (COUNTER < PREHEATLOOPS)
    {
        if ((millis() - START_INTERVAL_TIME) >= OPTO_INTERVAL) // start a new loop
        {
            // bSensorReadingFlag = true;
            // Reset interval timing to prepare the next reading
            START_INTERVAL_TIME += OPTO_INTERVAL; // millis();
            // Open LED channel
            iChannel = 0; // start reading from the 1st LED/Sensor
            // info_displayln("turn on 1st one");
            _LED.LED_on(iChannel);
            sensor67ValueTime = millis() + LED_DELAY_TIME + acquisitionControl.getRepeats() * 100;
        }
        else if (iChannel < OPTOCHANNELS) // if still reading 0~9
        {                                 // continue reading within one loop
            // check the sensor reading time
            if (millis() > sensor67ValueTime)
            {               // time to read sensor, check the err first
                if (errCnt) // if reading err, which means the channel is opened already
                {
                    if (errRereadTime > millis()) // not exceed 100ms yet
                    {
                        return;
                    }
                }
                else
                {
                    // info_displayf("channel %d, count %d\n", iChannel, COUNTER);
                    // Open channel
                    // if (iChannel > 4)
                    // {
                    //     I2CMux1.openChannel(I2C_Channel[4+5-iChannel]);
                    // }
                    // else
                    // {
                    //     I2CMux.openChannel(I2C_Channel[4-iChannel]);
                    // }
                }

                // Print RFU data
                // Word sensorResp = 0xFFFF;//VEML6035_GET_ALS_DATA();
                // bool flagres = VEML6035_GET_ALS_DATA_I2C_Res(&sensorResp);

                // Error checking
                //  if (flagres || (sensorResp == 0) || (sensorResp == 0xFFFF))     //if error happened, start the err process
                //  {
                //      // info_displayf("Reading error. flag: %d; resp: %d\n", flagres, sensorResp);
                //      errRereadTime = millis() + 100;     //try to read again after 100ms
                //      errCnt++;
                //      if (flagres)
                //      {
                //          errRecord[iChannel][0]++;
                //      }
                //      else if (sensorResp == 0)
                //      {
                //          errRecord[iChannel][1]++;
                //      }
                //      else
                //      {
                //          errRecord[iChannel][2]++;
                //      }

                //     if (errCnt > 10)
                //     {
                //         info_displayf("\n%dth opto sensor error!!! More details are shown as below:\n", iChannel+1);
                //         for (uint8_t i = 0; i < 10; i++)
                //         {
                //             for (uint8_t j = 0; j < 3; j++)
                //             {
                //                 if (errRecord[i][j])
                //                 {
                //                     info_displayf("Opto sensor %d reading err type %d for %d times\n", i+1, j, errRecord[i][j]);
                //                     errRecord[i][j] = 0;
                //                 }
                //             }
                //         }
                //         if (flagres)        // communication err
                //         {
                //             _displayCLD.ErrorProcessatBegin("Opto sensor error\n Please power off/on", String(iChannel+1));
                //         }
                //         else if (sensorResp == 0)   // sensor reading is zero, too dark or error
                //         {
                //             _displayCLD.ErrorProcessatBegin("Too dark\n Please check", String(iChannel+1));
                //         }
                //         else if (sensorResp == 0xFFFF)  //sensor reading maximum, too bright or error
                //         {
                //             _displayCLD.ErrorProcessatBegin("Too bright\n Please check", String(iChannel+1));
                //         }
                //         // ESP.restart();
                //         // rerun();
                //         _PIDControl.rerun();
                //         _sensor6035.rerun();
                //         return;
                //     }
                //     return;
                // }

                // float sensorvalue = (float(sensorResp)-FORTE_ORIGINS[iChannel])/FORTE_SLOPES[iChannel];
                // info_displayf("%f, ", sensorvalue);

                // Off LED
                _LED.LED_off(iChannel);

                // Close channel
                // if(iChannel > 4)
                // {
                //     I2CMux1.closeChannel(I2C_Channel[4+5-iChannel]);
                // }
                // else
                // {
                //     I2CMux.closeChannel(I2C_Channel[4-iChannel]);
                // }

                iChannel++;
                if (iChannel < OPTOCHANNELS)
                {
                    // On next LED
                    _LED.LED_on(iChannel);
                    sensor67ValueTime = millis() + LED_DELAY_TIME + acquisitionControl.getRepeats() * 100;
                }
                else
                {
                    // bSensorReadingFlag = false;                         //finish one round reading, heating during the interval
                    COUNTER++;
                    info_displayf("\nfinish %d rounds preheating\n", COUNTER);
                    if (COUNTER >= PREHEATLOOPS)
                    {
                        if (_PIDControl.getphase2ready())
                        {
                            info_display("heater is finished as well, update the display status\n");
                            _displayCLD.type_infor = ewaitampTube;
                            _displayCLD.changeScreen = true;
                            _buzzer.BuzzerAlert();
                        }
                        info_displayf("finish preheating, maintain the sensor heating\n");
                        // finish the reading, update the step
                        sensorStep = eSensormaintain; // preheat for 15mins already, enter maintain mode
                                                      //  info_displayln("eSensormaintain");

                        // eSensorParaIni();       //prepare for next loop at maintainance
                        // COUNTER = 0;
                        // iChannel = 0;
                        // sensor67ValueTime = millis() + LED_DELAY_TIME;
                        return;
                    }
                }
            }
        }
    }
    else
    {
        if (PREHEATLOOPS == 0) // in case the parameter is zero!
        {
            if (_PIDControl.getphase2ready())
            {
                info_display("heater is finished as well, update the display status\n");
                _displayCLD.type_infor = ewaitampTube;
                _displayCLD.changeScreen = true;
                _buzzer.BuzzerAlert();
            }
            info_displayf("finish preheating, maintain the sensor heating\n");
            // finish the reading, update the step
            sensorStep = eSensormaintain; // preheat for 15mins already, enter maintain mode
                                          //  info_displayln("eSensormaintain");

            // eSensorParaIni();       //prepare for next loop at maintainance
            // COUNTER = 0;
            // iChannel = 0;
            return;
        }
        else
        {
            info_displayln("Reading error, takes too long time");
        }
    }
}

void sensor6035::eSensorMaintain()
{
    if ((millis() - START_INTERVAL_TIME) >= OPTO_INTERVAL) // start a new loop
    {
        // Reset interval timing to prepare the next reading
        START_INTERVAL_TIME += OPTO_INTERVAL; // millis();
        // Open LED channel
        iChannel = 0; // start reading from the 1st LED/Sensor
        _LED.LED_on(iChannel);
        sensor67ValueTime = millis() + LED_DELAY_TIME + acquisitionControl.getRepeats() * 100;
    }
    else if (iChannel < OPTOCHANNELS) // if still reading 0~9
    {                                 // continue reading within one loop
        // check the sensor reading time
        if (millis() > sensor67ValueTime)
        {               // time to read sensor, check err first
            if (errCnt) // if reading err, which means the channel is opened already
            {
                if (errRereadTime > millis()) // not exceed 100ms yet
                {
                    return;
                }
            }
            else
            {
                // // info_displayf("channel %d, count %d\n", iChannel, COUNTER);
                // // Open channel
                // if (iChannel > 4)
                // {
                //     I2CMux1.openChannel(I2C_Channel[4+5-iChannel]);
                // }
                // else
                // {
                //     I2CMux.openChannel(I2C_Channel[4-iChannel]);
                // }
            }

            // Print RFU data
            // Word sensorResp = 0;//VEML6035_GET_ALS_DATA();
            // bool flagres = VEML6035_GET_ALS_DATA_I2C_Res(&sensorResp);

            // Error checking
            //  if (flagres || (sensorResp == 0) || (sensorResp == 0xFFFF))     //if error happened, start the err process
            //  {
            //      // info_displayf("Reading error. flag: %d; resp: %d\n", flagres, sensorResp);
            //      errRereadTime = millis() + 100;     //try to read again after 100ms
            //      errCnt++;
            //      if (flagres)
            //      {
            //          errRecord[iChannel][0]++;
            //      }
            //      else if (sensorResp == 0)
            //      {
            //          errRecord[iChannel][1]++;
            //      }
            //      else
            //      {
            //          errRecord[iChannel][2]++;
            //      }

            //     if (errCnt > 10)
            //     {
            //         info_displayf("\n%dth opto sensor error!!! More details are shown as below:\n", iChannel+1);
            //         for (uint8_t i = 0; i < 10; i++)
            //         {
            //             for (uint8_t j = 0; j < 3; j++)
            //             {
            //                 if (errRecord[i][j])
            //                 {
            //                     info_displayf("Opto sensor %d reading err type %d for %d times\n", i+1, j, errRecord[i][j]);
            //                     errRecord[i][j] = 0;
            //                 }
            //             }
            //         }
            //         if (flagres)        // communication err
            //         {
            //             _displayCLD.ErrorProcessatBegin("Opto sensor error\n Please power off/on", String(iChannel+1));
            //         }
            //         else if (sensorResp == 0)   // sensor reading is zero, too dark or error
            //         {
            //             _displayCLD.ErrorProcessatBegin("Too dark\n Please check", String(iChannel+1));
            //         }
            //         else if (sensorResp == 0xFFFF)  //sensor reading maximum, too bright or error
            //         {
            //             _displayCLD.ErrorProcessatBegin("Too bright\n Please check", String(iChannel+1));
            //         }
            //         // ESP.restart();
            //         // rerun();
            //         _PIDControl.rerun();
            //         _sensor6035.rerun();
            //         return;
            //     }
            //     return;
            // }

            // float sensorvalue = (float(sensorResp)-FORTE_ORIGINS[iChannel])/FORTE_SLOPES[iChannel];
            // info_displayf("%f, ", sensorvalue);

            // Off LED
            _LED.LED_off(iChannel);

            // Close channel
            // if(iChannel > 4)
            // {
            //     I2CMux1.closeChannel(I2C_Channel[4+5-iChannel]);
            // }
            // else
            // {
            //     I2CMux.closeChannel(I2C_Channel[4-iChannel]);
            // }

            iChannel++;
            if (iChannel < OPTOCHANNELS)
            {
                // On next LED
                _LED.LED_on(iChannel);
                sensor67ValueTime = millis() + LED_DELAY_TIME + acquisitionControl.getRepeats() * 100;
            }
            else
            {
                info_displayf("finish one round maintenance\n");
                // bSensorReadingFlag = false;                         //finish one round reading, heating during the interval
            }
        }
    }
}

void sensor6035::outputHeader()
{
    // print tag to announce start of amplification
    info_displayln("<AmpStart>");

    // write metadata in a json file
    const size_t capacity = 1000; // total buffer capacity
    DynamicJsonDocument metadata(capacity);
    JsonObject calibration = metadata.createNestedObject("calibration");
    JsonArray slopes = calibration.createNestedArray("slopes");
    JsonArray origins = calibration.createNestedArray("origins");
    JsonArray ledPower = metadata.createNestedArray("led_power");
    for (int i = 0; i < OPTOCHANNELS; i++)
    {
        slopes.add(FORTE_SLOPES[i]);
        origins.add(FORTE_ORIGINS[i]);
        uint8_t *LED_PWM_VALUE_SETTING = _ForteSetting.parameter.led_power;
        ledPower.add(LED_PWM_VALUE_SETTING[i]);
    }
    metadata["units"] = OpticalUnits;
    metadata["device_id"] = protoID;
    metadata["slots"] = OPTOCHANNELS;
    metadata["amplification_time"] = float(OPTO_DURATION_2) / 60000;
    metadata["reading_interval"] = float(OPTO_INTERVAL) / 60000;
    metadata["software_version"] = FirmwareVer;

    // Output metadata
    String output;
    serializeJson(metadata, output);
    info_displayln(output);

    // Print heading
    const String header = "Amplification Time[min],Sensor 1 Fluorescence[nM FAM],Sensor 2 Fluorescence[nM FAM],Sensor 3 Fluorescence[nM FAM],Sensor 4 Fluorescence[nM FAM],Sensor 5 Fluorescence[nM FAM],Temperature[C]";
    info_displayln(header);
}

void sensor6035::closeSensorChannel(int slot)
{
    // delay(200);
    if (slot > 4)
    {
        I2CMux1.closeChannel(I2C_Channel[4 + 5 - slot]);
    }
    else
    {
        I2CMux.closeChannel(I2C_Channel[4 - slot]);
    }
}

void sensor6035::openSensorChannel(int slot)
{
    if (slot > 4)
    {
        I2CMux1.openChannel(I2C_Channel[4 + 5 - slot]);
    }
    else
    {
        I2CMux.openChannel(I2C_Channel[4 - slot]);
    }
    // delay(200);
}

void sensor6035::eSensorstartFunc()
{
    // switch off all LED first, in case some is still open
    for (uint8_t i = 0; i < 10; i++)
    {
        _LED.LED_off(i); // turn off the LED
        // openSensorChannel(i);
        // VEML6035_SET_SD(VEML6035_ALS_SD_OFF);
        // closeSensorChannel(i);
    }

    // reconfigure all sensors in case on lost configuration during
    I2CMux.closeAll();
    I2CMux1.closeAll();

    acquisitionControl.clear();

    // reConfigSensors();

    outputHeader();

    // stop hotlid23 heating to make sure the measurement to be stable
    bSensorReadingFlag = true; // indicate the sensor reading status, to stop the hotlid heatup, to save the power for it
    // Record start time for duration and interval
    START_INTERVAL_TIME = millis();
    COUNTER = 0;
    info_display(float(COUNTER) * OPTO_INTERVAL / 60000);
    info_display(",");
    // turn on the 1st LED
    iChannel = 0;
    // connectToSensor(iChannel);
    // openSensorChannel(iChannel);
    // // VEML6035_SET_ALS_IT(VEML6035_ALS_IT_800ms);
    // VEML6035_SET_SD(VEML6035_ALS_SD_ON);
    // delay(200);
    _LED.LED_on(iChannel); // turn on the 1st LED
    acquisitionControl.clear();
    // closeSensorChannel(iChannel);
    sensor67ValueTime = millis() + LED_DELAY_TIME; // the time that sensor can read
    sensorStep = eSensor1stReading;                // change to the step to start the measurement
    // info_displayln("eSensormaintain");
    tic = millis();
}

void sensor6035::eSensor1stReadingFunc()
{
    // if((millis() - START_DURATION_TIME) <= OPTO_DURATION+50*1000)       //50*1000 is used as redundancy in case time is not enough for the sensor reading
    if (COUNTER < MEASUREMENTLOOPS)
    {
        if ((millis() - START_INTERVAL_TIME) >= OPTO_INTERVAL) // start a new loop
        {
            bSensorReadingFlag = true;
            // Reset interval timing to prepare the next reading
            START_INTERVAL_TIME += OPTO_INTERVAL; // millis();
            // Print timing interval
            if (flagCounterDisplay)
            {
                info_display(float(COUNTER)); // send counter
            }
            else
            {
                info_display(float(COUNTER) * OPTO_INTERVAL / 60000); // send time
            }
            info_display(",");

            // Open LED channel
            iChannel = 0; // start reading from the 1st LED/Sensor
            _LED.LED_on(iChannel);
            tic = millis();
            // _LED.LED_on(iChannel);
            sensor67ValueTime = millis() + LED_DELAY_TIME;
        }
        else if (iChannel < OPTOCHANNELS) // if still reading 0~9
        {                                 // continue reading within one loop
            // check the sensor reading time
            if (millis() > sensor67ValueTime) // check if the duration of LED
            {                                 // time to read sensor
                // check the error status
                if (errCnt) // if reading err, which means the channel is opened already
                {
                    if (errRereadTime > millis()) // not exceed 100ms yet
                    {
                        return;
                    }
                }

                if (acquisitionControl.isClear())
                {
                    openSensorChannel(iChannel);
                    // info_display("<");
                }

                if (!acquisitionControl.isFinished())
                {

                    bool flagres;
                    Word sensorResp = 0xFFFF;
                    flagres = VEML6035_GET_ALS_DATA_I2C_Res(&sensorResp);
                    if (!flagres)
                    {
                        acquisitionControl.store(sensorResp);
                        // info_display(sensorResp);
                        // info_display(",");
                        // info_display("<");
                        // info_display(millis() - tic);
                        // info_display(">");
                        tic = millis();
                    }
                    else
                    {
                        acquisitionControl.addErrorCount();
                    }
                    if (acquisitionControl.isMaxErrorReached())
                    {
                        _displayCLD.ErrorProcessatBegin("Opto sensor error\n Please power off/on", String(iChannel + 1));
                        ESP.restart();
                        rerun();
                        _PIDControl.rerun();
                        _sensor6035.rerun();
                        return;
                    }
                    // add 100 ms to measurement time
                    sensor67ValueTime = millis() + 100;
                }
                else
                {
                    // info_display(">");
                    Word meanResponse = acquisitionControl.getSum();
                    closeSensorChannel(iChannel);
                    acquisitionControl.clear();
                    _LED.LED_off(iChannel);
                    // finish one session, clear the err counter
                    errCnt = 0;
                    oddCnt = 0;
                    errRereadTime = 0;
                    // convert the value and output
                    sensor67Value[iChannel][COUNTER] = meanResponse;
                    // sensor67Value[iChannel][COUNTER] = (float(sensorResp)-calMatrix[iChannel][1])/calMatrix[iChannel][0];
                    info_display((float(meanResponse) - FORTE_ORIGINS[iChannel]) / FORTE_SLOPES[iChannel]);
                    info_display(",");

                    iChannel++;
                    if (iChannel < OPTOCHANNELS)
                    {
                        _LED.LED_on(iChannel);
                        // connectToSensor(iChannel);
                        // On next LED

                        // closeSensorChannel(iChannel);
                        sensor67ValueTime = millis() + LED_DELAY_TIME;
                        tic = millis();
                    }
                    else
                    {
                        info_displayln(_PIDControl.getBottomTemperature()[1]); // display current tempeature
                        bSensorReadingFlag = false;                            // finish one round reading, heating during the interval
                        COUNTER++;
                        if (COUNTER >= MEASUREMENTLOOPS) // OPTO_DURATION/OPTO_INTERVAL)
                        {
                            // finish the reading, update the step
                            sensorStep = eSensormaintain; // only read during amplification
                            // info_displayln("eSensormaintain");
                            COUNTER = 0;
                            // iChannel = 0;
                            _displayCLD.type_infor = escreenFinished;
                            _displayCLD.bheadershow = true;
                            _displayCLD.changeScreen = true;
                            EEPROM.begin(_EEPROM_SIZE);
                            Word tmp[10 * 130] = {0};

                            memcpy(tmp, sensor67Value, sizeof(tmp));

                            EEPROM.put(RECORDPOS, tmp);
                            delay(100);
                            EEPROM.commit();
                            delay(100);
                            EEPROM.end();
                            delay(100);

                            _buzzer.BuzzerAlert();

                            // _PIDControl.StopHeating();       //comment here so the heater will maintainthe temperature
                            // _PIDControl.setepidfinish();       //comment here so the heater will maintainthe temperature

                            // Announce End of amplification
                            info_displayln("<AmpStart/>");
                            // check the opto read err
                            for (uint8_t i = 0; i < 10; i++)
                            {
                                for (uint8_t j = 0; j < 3; j++)
                                {
                                    if (errRecord[i][j])
                                    {
                                        info_displayf("Opto sensor %d reading err type %d for %d times\n", i + 1, j, errRecord[i][j]);
                                        errRecord[i][j] = 0;
                                    }
                                }
                            }

                            return;
                        }
                    }
                }
            }
        }
    }
    else
    {
        info_displayln("Reading error, takes too long time");
    }
}

void sensor6035::clear()
{
    // VEML6035_SET_SD(VEML6035_ALS_SD_OFF);
    // closeSensorChannel(iChannel);
    START_INTERVAL_TIME = 0;
    COUNTER = 0;
    iChannel = 0;
    for (size_t i = 0; i < 7; i++)
    {
        SENSOR_DATA[i] = 0;
    }

    uint8_t loops = _ForteSetting.parameter.amplification_time;

    for (size_t i = 0; i < 10 * loops; i++)
    {
        sensor67Value[i / loops][i % loops] = 0;
    }
}

void sensor6035::switchSensorAcquisitionState(bool state)
{
    if (state == true)
    {
        VEML6035_SET_SD(VEML6035_ALS_SD_ON);
    }
    else
    {
        VEML6035_SET_SD(VEML6035_ALS_SD_OFF);
    }
}

void sensor6035::switchAllSensorsAcquisitionState(bool state)
{
    // process the 1st 5 sensors
    for (int iChannel = 0; iChannel < 5; iChannel++)
    {
        I2CMux.openChannel(I2C_Channel[iChannel]);
        switchSensorAcquisitionState(state);
        delay(10);
        I2CMux.closeChannel(I2C_Channel[iChannel]);
    }
    // process the 2nd 5 sensors
    for (int iChannel = 0; iChannel < 5; iChannel++)
    {
        I2CMux1.openChannel(I2C_Channel[iChannel]);
        switchSensorAcquisitionState(state);
        delay(10);
        I2CMux1.closeChannel(I2C_Channel[iChannel]);
    }
}

bool sensor6035::checkSensorConfiguration()
/*
Function that checks that the configuration is as expected.
The error reporting is a poor design. To be improved later on.
*/
{
    if (VEML6035_GET_ALS_IT_Bits() != 6)
    {
        return false;
    }
    if (VEML6035_GET_GAIN_Bit() != 1)
    {
        return false;
    }
    if (VEML6035_GET_DG_Bit() != 0)
    {
        return false;
    }
    if (VEML6035_GET_SENS_Bit() != 0)
    {
        return false;
    }
    if (VEML6035_GET_ALS_Mode() != 0)
    {
        return false;
    }
    return true;
}

void sensor6035::reConfigSingleSensor(int slot)
/*
To reconfigure a single sensor. It requires all I2C channels to be closed prior to call.
*/
{
    openSensorChannel(slot);
    // switchSensorAcquisitionState(true);
    VEML6035_SET_CHANNEL_EN(Config_CHANNEL);
    VEML6035_SET_ALS_IT(Config_ALS_IT);
    VEML6035_SET_GAIN(Config_GAIN);
    VEML6035_SET_DG(Config_DG);
    VEML6035_SET_SENS(Config_SENS);
    closeSensorChannel(slot);

    // switchSensorAcquisitionState(true);
}

void sensor6035::reConfigSensors()
{
    // close all channels
    I2CMux.closeAll();
    I2CMux1.closeAll();

    // soft reset channels
    delay(200);
    ResetAllSensors(); // reset all 10 sensors
    delay(200);

    // set configuration
    for (int slot = 0; slot < 10; slot++)
    {
        reConfigSingleSensor(slot);
    }
}

void sensor6035::connectToSensor(int slot)
{
    openSensorChannel(slot);
    if (!checkSensorConfiguration())
    {
        info_displayln("[W] Re-establishing sensor configuration.")
            reConfigSingleSensor(slot);
        openSensorChannel(slot);
    }
    // VEML6035_SET_ALS_IT(VEML6035_ALS_IT_800ms);
    // VEML6035_SET_SD(VEML6035_ALS_SD_ON);
    delay(10);
}

void sensor6035::disconnectFromSensor(int slot)
{
    VEML6035_SET_SD(VEML6035_ALS_SD_OFF);
    delay(10);
    closeSensorChannel(slot);
}

void sensor6035::testShot(int slot)
{

    _LED.LED_OFF_ALL();
    // openSensorChannel(slot);
    // VEML6035_SET_SD(VEML6035_ALS_SD_ON);
    _LED.LED_on(slot);

    Word sensorResp;
    bool flagres;
    Word meanResponse = 0xFFFF;
    openSensorChannel(slot);
    // delay(100);

    acquisitionControl.clear();
    //  info_display("<");

    while (!acquisitionControl.isFinished())
    {
        sensorResp = 0xFFFF;
        flagres = VEML6035_GET_ALS_DATA_I2C_Res(&sensorResp);
        // info_display(sensorResp);
        // info_display(",");
        if (!flagres)
        {
            acquisitionControl.store(sensorResp);
        }
        else
        {
            acquisitionControl.addErrorCount();
        }
        if (acquisitionControl.isMaxErrorReached())
        {
            _displayCLD.ErrorProcessatBegin("Opto sensor error\n Please power off/on", String(iChannel + 1));
            return;
        }
        delay(100);
    }

    meanResponse = acquisitionControl.getSum();

    // Word sensorResp = 0xFFFF;
    // float integratedResponse = 0.0;
    // bool flagres;
    // uint8_t repeats = 8;
    // openSensorChannel(slot);
    // delay(400);
    // info_display("{shots: ")
    // for(uint8_t i=0; i<repeats;i++)
    // {
    //     flagres = VEML6035_GET_ALS_DATA_I2C_Res(&sensorResp);
    //     integratedResponse += sensorResp;
    //     info_display(sensorResp);
    //     info_display(", ");
    //     delay(100);
    // }
    // info_displayln("}");
    // integratedResponse /= repeats;

    closeSensorChannel(slot);
    info_display("{Green: ");
    info_display(meanResponse);
    info_displayln("}");
    // Snapshot();
    // delay(100);
    _LED.LED_off(slot);
    // VEML6035_SET_SD(VEML6035_ALS_SD_OFF);

    // info_display(VEML6035_GET_ALS_IT());
    // info_display(", ");
    // info_display(VEML6035_GET_GAIN());
    // info_display(", ");
    // info_display(VEML6035_GET_DG());
    // info_display(", ");
    // info_display(VEML6035_GET_SENS());
    // info_display(", ");
    // info_display(VEML6035_GET_CHANNEL_EN_Bit());
    // info_display(", ");
    // info_display(VEML6035_GET_Delay());
    // info_display(", ");
    // info_display(VEML6035_GET_ALS_Mode());
    // info_display(", ");
    // info_display(VEML6035_GET_PSM_EN_Bit());
    // // closeSensorChannel(slot);
    // info_displayln("////////////////");
}

void sensor6035::OptoCommandProcess(char command)
{
    int _LED_SLOT;
    // Switch statement to execute different functions based on the received command

    switch (command)
    {
    case 'R':
        reConfigSensors();
        break;

    case 'P':
        // Prompt the user to enter a new PWM value
        info_display("Enter LED slot (0-9): ");

        // Wait for user input
        while (Serial.available() == 0)
        {
        }

        // Read the user input as an integer
        _LED_SLOT = Serial.parseInt();
        info_displayln(_LED_SLOT);

        // Print the captured PWM
        info_display("LED slot is: ");
        info_displayln(_LED_SLOT);

        // Let the user know the current LED PWM
        info_display("Current PWM value (%) set to: ");
        info_displayln(_LED.getPWMValue(_LED_SLOT));

        // Prompt the user to enter a new PWM value
        info_display("Enter new PWM value (%, integer) for LED : ");

        // Wait for user input
        while (Serial.available() == 0)
        {
        }

        // Read the user input as an integer
        _LED.setPWMValue(_LED_SLOT, Serial.parseInt());

        // Print the captured PWM
        info_display("New PWM value (%) set to: ");
        info_displayln(_LED.getPWMValue(_LED_SLOT));
        break;
    case '0':
        testShot(0);
        break;
    case '1':
        testShot(1);
        break;
    case '2':
        testShot(2);
        break;
    case '3':
        testShot(3);
        break;
    case '4':
        testShot(4);
        break;
    case '5':
        testShot(5);
        break;
    case '6':
        testShot(6);
        break;
    case '7':
        testShot(7);
        break;
    case '8':
        testShot(8);
        break;
    case '9':
        testShot(9);
        break;
    case 'A':
        info_display("Current PWM value (/255) for HEATER 1 is  set to: ");
        info_displayln(_heater1_PWM);

        // Prompt the user to enter a new PWM value
        info_display("Enter new PWM value (/255, integer) for the heater : ");

        // Wait for user input
        while (Serial.available() == 0)
        {
        }

        // Read the user input as an integer
        _heater1_PWM = Serial.parseInt();
        analogWrite(HEATER1IO, _heater1_PWM);

        // Print the captured PWM
        info_display("New PWM value (/255) set to: ");
        info_displayln(_heater1_PWM);
        break;
    case 'B':
        // Let the user know the current PWM value
        info_display("Current PWM value (/255) for HEATER 2 is  set to: ");
        info_displayln(_heater2_PWM);

        // Prompt the user to enter a new PWM value
        info_display("Enter new PWM value (/255, integer) for the heater : ");

        // Wait for user input
        while (Serial.available() == 0)
        {
        }

        // Read the user input as an integer
        _heater2_PWM = Serial.parseInt();
        analogWrite(HEATER2IO, _heater2_PWM);

        // Print the captured PWM
        info_display("New PWM value (/255) set to: ");
        info_displayln(_heater2_PWM);
        break;
    case 'C':
        // Let the user know the current PWM value
        info_display("Current PWM value (/255) for HEATER 3 is  set to: ");
        info_displayln(_heater3_PWM);

        // Prompt the user to enter a new PWM value
        info_display("Enter new PWM value (/255, integer) for the heater : ");

        // Wait for user input
        while (Serial.available() == 0)
        {
        }

        // Read the user input as an integer
        _heater3_PWM = Serial.parseInt();
        analogWrite(HEATER3IO, _heater3_PWM);

        // Print the captured PWM
        info_display("New PWM value (/255) set to: ");
        info_displayln(_heater3_PWM);
        break;

        // case 'D':
        //     // Let the user know the current PWM value
        //     info_display("Current PWM value (/255) for TOP HEATER 1  is  set to: ");
        //     info_displayln(_top_heater1_PWM);

        //     // Prompt the user to enter a new PWM value
        //     info_display("Enter new PWM value (/255, integer) for the heater : ");

        //     // Wait for user input
        //     while (Serial.available() == 0)
        //     {
        //     }

        //     // Read the user input as an integer
        //     _top_heater1_PWM = Serial.parseInt();
        //     analogWrite(HOTLID1IO, _top_heater1_PWM);

        //     // Print the captured PWM
        //     info_display("New PWM value (/255) set to: ");
        //     info_displayln(_top_heater1_PWM);
        //     break;

    case 'E':
        // Let the user know the current PWM value
        info_display("Current PWM value (/255) for TOP HEATER 2  is  set to: ");
        info_displayln(_top_heater2_PWM);

        // Prompt the user to enter a new PWM value
        info_display("Enter new PWM value (/255, integer) for the heater : ");

        // Wait for user input
        while (Serial.available() == 0)
        {
        }

        // Read the user input as an integer
        _top_heater2_PWM = Serial.parseInt();
        analogWrite(HOTLID23IO, _top_heater2_PWM);

        // Print the captured PWM
        info_display("New PWM value (/255) set to: ");
        info_displayln(_top_heater2_PWM);
        break;

    case 'F':
        // Let the user know the current PWM value
        info_display("Current PWM value (/255) for TOP HEATER 3  is  set to: ");
        info_displayln(_top_heater3_PWM);

        // Prompt the user to enter a new PWM value
        info_display("Enter new PWM value (/255, integer) for the heater : ");

        // Wait for user input
        while (Serial.available() == 0)
        {
        }

        // Read the user input as an integer
        _top_heater3_PWM = Serial.parseInt();
        analogWrite(HOTLID23IO, _top_heater3_PWM);

        // Print the captured PWM
        info_display("New PWM value (/255) set to: ");
        info_displayln(_top_heater3_PWM);
        break;

    case 'M':
        info_displayln("Printing calibration:");
        // write metadata in a json file
        const size_t capacity = 1000; // total buffer capacity
        DynamicJsonDocument metadata(capacity);
        JsonObject calibration = metadata.createNestedObject("calibration");
        JsonArray slopes = calibration.createNestedArray("slopes");
        JsonArray origins = calibration.createNestedArray("origins");
        JsonArray ledPower = metadata.createNestedArray("led_power");
        for (int i = 0; i < OPTOCHANNELS; i++)
        {
            slopes.add(FORTE_SLOPES[i]);
            origins.add(FORTE_ORIGINS[i]);
            // slopes.add(calMatrix[i][1]);
            uint8_t *LED_PWM_VALUE_SETTING = _ForteSetting.parameter.led_power;
            ledPower.add(LED_PWM_VALUE_SETTING[i]);
        }
        metadata["units"] = OpticalUnits;
        metadata["device ID"] = protoID;
        metadata["slots"] = OPTOCHANNELS;
        metadata["amplification_time"] = float(OPTO_DURATION_2) / 60000;
        metadata["reading_interval"] = float(OPTO_INTERVAL) / 60000;
        metadata["software_version"] = FirmwareVer;

        // Output metadata
        String output;
        serializeJson(metadata, output);
        info_displayln(output);
        break;
    }
}

bool sensor6035::bSensorReadingGet()
{
    return bSensorReadingFlag;
}

//////////////////Calibration
float sensor6035::calib_sensor(int slot)
{
    _LED.LED_OFF_ALL();
    _LED.LED_on(slot);

    Word sensorResp;
    bool flagres;
    Word meanResponse = 0xFFFF;
    openSensorChannel(slot);

    acquisitionControl.clear();

    while (!acquisitionControl.isFinished())
    {
        sensorResp = 0xFFFF;
        flagres = VEML6035_GET_ALS_DATA_I2C_Res(&sensorResp);

        if (!flagres)
        {
            acquisitionControl.store(sensorResp);
        }
        else
        {
            acquisitionControl.addErrorCount();
        }
        if (acquisitionControl.isMaxErrorReached())
        {
            _displayCLD.ErrorProcessatBegin("Opto sensor error\n Please power off/on", String(iChannel + 1));
            return -1;
        }
        delay(100);
    }

    meanResponse = acquisitionControl.getSum();

    closeSensorChannel(slot);
    _LED.LED_off(slot);

    return (float)meanResponse;
}

void sensor6035::calibration(int slot)
{
    if (type_calib < 3)
    {
        _displayCLD.display_Waiting_Calib();
        result_calib[type_calib] = calib_sensor(slot);
        type_calib += 1;
        _displayCLD.type_infor = eCalibrating;
        _displayCLD.changeScreen = true;
    }
    else
    {
        _displayCLD.display_Waiting_Calib();
        result_calib[type_calib] = calib_sensor(slot);
        calculate_calib(result_calib);
        type_calib = 0;
        _displayCLD.type_infor = eCalibComplete;
        _displayCLD.changeScreen = true;
    }
    setStepeSensorwait();
}

void sensor6035::calculate_calib(float y[])
{
    float x[4] = {300.0, 200.0, 100.0, 0.0};
    int n = 4;
    float sum_x = 0, sum_y = 0, sum_xy = 0, sum_x2 = 0, sum_y2 = 0, mean_x = 0, mean_y = 0;

    for (int i = 0; i < n; i++)
    {
        sum_x += x[i];
        sum_y += y[i];
        sum_xy += x[i] * y[i];
        sum_x2 += x[i] * x[i];
        sum_y2 += y[i] * y[i];
    }

    // Tính toán giá trị trung bình
    mean_x = sum_x / n;
    mean_y = sum_y / n;

    // Tính slope
    float slope = (n * sum_xy - sum_x * sum_y) / (n * sum_x2 - sum_x * sum_x);
    cal_calib[0] = slope;

    // Tính RSQ
    float RSQ = ((n * sum_xy - sum_x * sum_y) * (n * sum_xy - sum_x * sum_y)) / ((n * sum_x2 - sum_x * sum_x) * (n * sum_y2 - sum_y * sum_y));
    cal_calib[1] = RSQ;

    // Tính origins
    float origin = mean_y - slope * mean_x;
    cal_calib[2] = origin;
}

void sensor6035::setStepeSensorcalib()
{
    sensorStep = eSensorcalib;
}
void sensor6035::setStepeSensorwait()
{
    sensorStep = eSensorwait;
}

int I2C_Bus = 3;
sensor6035 _sensor6035;
