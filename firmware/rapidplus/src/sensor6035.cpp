#include "sensor6035.h"
#include "LED.h"
#include "define.h"
#include "PIDControl.h"
#include "Bluetooth.h"
#include "errorCheck.h"
// #include "ArduinoJson.h"

/***********************************************************************
 * Function: sensor6035()
 * Description: Default constructor for the sensor6035 class. Performs no
 *  work; all hardware/state initialization happens later in begin().
 * pramameter: none
 *  return: none
 */
sensor6035::sensor6035(/* args */)
{
}

/***********************************************************************
 * Function: ~sensor6035()
 * Description: Destructor for the sensor6035 class. No dynamic resources
 *  are owned, so it performs no cleanup.
 * pramameter: none
 *  return: none
 */
sensor6035::~sensor6035()
{
}

/***********************************************************************
 * Function: begin()
 * Description: One-time startup of the 10-channel VEML6035 opto subsystem:
 *  initializes the LED driver and both I2C multiplexers, computes the
 *  PCB-dependent I2C channel sequence, soft-resets all 10 sensors, then
 *  runs the full configuration chain (channel enable, ALS integration
 *  time, gain, digital gain, sensitivity) and a one-loop snapshot self
 *  test. Finally seeds the state machine into eSensorpreheat, turns on
 *  the first LED and starts the interval/reading timers.
 * pramameter: none
 *  return: none
 */
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
    _LED.LED_on_unguarded(0); // turn on the first LED
    iChannel = 0;
    sensor67ValueTime = millis() + LED_DELAY_TIME; // initialize the timer
    START_INTERVAL_TIME = millis();                // as start preheat immediatly, need to record the time together
}

/***********************************************************************
 * Function: loop()
 * Description: Main non-blocking state-machine dispatcher called every
 *  firmware cycle. Branches on sensorStep to drive the opto workflow:
 *  preheat, maintain, start, first reading/amplification acquisition or
 *  calibration. Does nothing while in eSensorwait.
 * pramameter: none
 *  return: none
 */
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

/***********************************************************************
 * Function: skip2Maintain()
 * Description: Force-advances the state machine from any pre-maintain
 *  step directly to eSensormaintain (skipping the remaining 15-min
 *  preheat) and flags the display to show the header and redraw.
 * pramameter: none
 *  return: none
 */
void sensor6035::skip2Maintain()
{
    if (sensorStep < eSensormaintain)
    {
        sensorStep = eSensormaintain; // preheat for 15mins already, enter maintain mode
        _displayCLD.bheadershow = true;
        _displayCLD.changeScreen = true;
    }
}

/***********************************************************************
 * Function: rerun()
 * Description: Re-entry helper used when restarting a run. If the sensors
 *  have already preheated, it turns off all LEDs, clears the acquired
 *  data buffers and drops the state machine back to eSensormaintain so
 *  the sensors only need to be kept warm rather than re-preheated.
 * pramameter: none
 *  return: none
 */
void sensor6035::rerun()
{
    if (sensorStep > eSensorpreheat) // if opto has preheat, then just maintain it
    {
        // info_displayln("sensor only need to maintain status");
        _LED.LED_OFF_ALL_unguarded();
        clear();
        sensorStep = eSensormaintain;
    }
}

/***********************************************************************
 * Function: setCounterDisplayflag()
 * Description: Setter that selects whether amplification output is
 *  labelled by raw loop counter or by elapsed time during the first
 *  reading step (see eSensor1stReadingFunc()).
 * pramameter: flag - true to print the loop counter, false to print time
 *  return: none
 */
void sensor6035::setCounterDisplayflag(bool flag)
{
    flagCounterDisplay = flag;
}

/***********************************************************************
 * Function: setStepeSensorpreheat()
 * Description: Transitions the state machine from eSensorwait into
 *  eSensorpreheat and re-initializes the preheat parameters/timers via
 *  eSensorParaIni(). No-op if not currently waiting.
 * pramameter: none
 *  return: none
 */
void sensor6035::setStepeSensorpreheat()
{
    if (sensorStep == eSensorwait)
    {
        sensorStep = eSensorpreheat;
        // info_displayln("setpreheat");
        eSensorParaIni();
    }
}

/***********************************************************************
 * Function: setStepeSensorstart()
 * Description: Idempotent guard that advances the state machine into
 *  eSensorstart only from a pre-measurement state. Once eSensor1stReading
 *  has been reached it deliberately does nothing, preventing a re-entry
 *  of eSensorstartFunc() that would zero COUNTER, clear acquired data and
 *  wipe the error EEPROM mid-run.
 * pramameter: none
 *  return: none
 */
void sensor6035::setStepeSensorstart()
{
    // Idempotent guard: only transition forward into eSensorstart from a
    // pre-measurement state. Once the sensor has reached eSensor1stReading,
    // calling this again must NOT roll it back — doing so would re-enter
    // eSensorstartFunc() which zeroes COUNTER, clears acquired data and
    // wipes the error EEPROM. Critical for the manual chord-reinit and
    // any future caller during an active 40-min amplification run.
    if (sensorStep < eSensorstart)
    {
        sensorStep = eSensorstart;
    }
}

#define BREAKING_START_INDEX 6
#define RISING_WINDOW 6
/***********************************************************************
 * Function: bResultGet()
 * Description: Runs the full amplification-curve analysis algorithm for
 *  all 10 slots. Loads the algorithm parameters from the JSON string in
 *  flash, converts each slot's raw sensor67Value samples into calibrated
 *  fluorescence using FORTE_ORIGINS/FORTE_SLOPES, checks for break and
 *  rising data, post-processes (baseline, Savitzky-Golay smoothing),
 *  differentiates, detects the sigmoidal feature and predicts the
 *  outcome. Marks slots as "Break" when invalid, then fills the output
 *  arrays with each slot's transition (CT) time and outcome character.
 * pramameter: CT_value - output array (10) receiving each slot's CT/
 *  transition time;
 *  result - output array (10) receiving each slot's outcome character
 *  (e.g. 'N', 'P', 'B'reak)
 *  return: true on success, false if JSON deserialization failed
 */
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

        // --------------------------------------------------
        auto timeBegin = recordIn.time_data.begin();
        auto timeEnd = recordIn.time_data.end();
        auto rawBegin = recordIn.raw_data.begin();
        auto rawEnd = recordIn.raw_data.end();

        bool validForDetection = true;

        /*  Check if there is a valid break and rising data
            If a BREAK is detected in POSITIVE data, process the data array to exclude the BREAK.*/
        size_t breakIndex = check_breakData(recordIn.raw_data, recordIn.parameters.min_increase / FORTE_SLOPES[i], BREAKING_START_INDEX);
        size_t risingIndex = check_risingData(recordIn.raw_data, recordIn.parameters.detection_margin_time, RISING_WINDOW);
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
                /* No valid rising data found */
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
        // else
        // {
        //     recordOut.peak_features.clear();
        //     recordOut.outcome.transition_time.clear();
        //     recordOut.outcome.plateau_point.clear();
        //     strcpy(recordOut.outcome.outcome, "Break");
        // }

        if (!validForDetection ||
            (breakIndex && risingIndex && recordOut.outcome.outcome[0] == 'N'))
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

/***********************************************************************
 * Function: bResultPutToChart()
 * Description: Same per-slot amplification analysis as bResultGet(), but
 *  in addition to the CT time and outcome it returns the full smoothed
 *  (post-processed) curve for charting. For each of the 10 slots it
 *  allocates a loops-long float buffer with malloc and copies the
 *  processed_data into it for the caller to plot.
 * pramameter: CT_value - output array (10) receiving each slot's CT/
 *  transition time;
 *  result - output array (10) receiving each slot's outcome character;
 *  processed_data - output array of 10 float* pointers, each malloc'd
 *  here with the smoothed curve (caller must free)
 *  return: true on success, false if JSON deserialization or a malloc
 *  failed
 */
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
        /* ----------------------------- */
        size_t breakIndex = check_breakData(recordIn.raw_data, recordIn.parameters.min_increase / FORTE_SLOPES[i], BREAKING_START_INDEX);
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

        if (!validForDetection ||
            (breakIndex && risingIndex && recordOut.outcome.outcome[0] == 'N'))
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
        // JsonDocument jsonOut = recordOut.toJSON();
        delay(100);
        // serializeJson(jsonOut, Serial);
        info_displayln();
        info_displayln();
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

/***********************************************************************
 * Function: bResultPutToGoogleSheet()
 * Description: Same per-slot amplification analysis as bResultGet(), but
 *  additionally exports the rich diagnostic structures for upload to a
 *  Google Sheet. For each of the 10 slots it copies the full
 *  DiagnosticOutcome and detected FeatureDetection (peak features) into
 *  the caller-provided arrays along with the CT time and outcome char.
 * pramameter: CT_value - output array (10) of each slot's CT/transition
 *  time;
 *  result - output array (10) of each slot's outcome character;
 *  get_outcome - output array (10) receiving each slot's full
 *  DiagnosticOutcome;
 *  get_peak_features - output array (10) receiving each slot's
 *  FeatureDetection peak features
 *  return: true on success, false if JSON deserialization failed
 */
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
        size_t breakIndex = check_breakData(recordIn.raw_data, recordIn.parameters.min_increase / FORTE_SLOPES[i], BREAKING_START_INDEX);
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
        // else
        // {
        //     recordOut.peak_features.clear();
        //     recordOut.outcome.transition_time.clear();
        //     recordOut.outcome.plateau_point.clear();
        //     strcpy(recordOut.outcome.outcome, "Break");
        // }

        if (!validForDetection ||
            (breakIndex && risingIndex && recordOut.outcome.outcome[0] == 'N'))
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
        // JsonDocument jsonOut = recordOut.toJSON();
        delay(100);
        // serializeJson(jsonOut, Serial);
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

/***********************************************************************
 * Function: AlgLoop()
 * Description: Offline/debug algorithm runner that takes a JSON payload
 *  (parameters plus raw fluorescence curve) received over serial,
 *  deserializes it, runs the single-curve pipeline (post-process,
 *  differentiate, find sigmoidal feature, predict outcome) and serializes
 *  the resulting record back to Serial as JSON. Used to test the
 *  algorithm without live sensor acquisition.
 * pramameter: recvData - C-string containing the JSON input record
 *  return: none
 */
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

/***********************************************************************
 * Function: eSensorParaIni()
 * Description: Resets the per-loop preheat acquisition state: zeroes
 *  COUNTER and the channel index, and (only on the very first call, when
 *  sensor67ValueTime is still zero) turns on the first LED and seeds the
 *  reading timer. Always records START_INTERVAL_TIME as the loop start.
 * pramameter: none
 *  return: none
 */
void sensor6035::eSensorParaIni()
{
    // START_INTERVAL_TIME = 0;
    COUNTER = 0;
    iChannel = 0;
    if (sensor67ValueTime == 0) // only for the first time when sensor67ValueTime is not initialized yet
    {
        _LED.LED_on_unguarded(0);                      // turn on the first LED
        sensor67ValueTime = millis() + LED_DELAY_TIME; // initialize the timer
        info_display("Set sensor67ValueTime when it's zero\n");
    }
    START_INTERVAL_TIME = millis(); // as start preheat immediatly, need to record the time together
}

/***********************************************************************
 * Function: getSensorPreheatReady()
 * Description: Query helper reporting whether the sensors have finished
 *  preheating, i.e. whether the state machine has reached
 *  eSensormaintain.
 * pramameter: none
 *  return: true if sensorStep == eSensormaintain (preheat complete)
 */
bool sensor6035::getSensorPreheatReady()
{
    // info_displayf("sensor step %d:%d\n", sensorStep, eSensormaintain)
    return sensorStep == eSensormaintain;
}

// Auto/Self-Timed Mode Basic Initialization Function
/***********************************************************************
 * Function: Basic_Initialization_Auto_Mode()
 * Description: Minimal VEML6035 bring-up used during reset: enables the
 *  ALS channel only (white channel disabled) and powers the ALS sensor
 *  on, then waits 100ms for it to settle. Assumes the target sensor's
 *  I2C channel is already open.
 * pramameter: none
 *  return: none
 */
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
/***********************************************************************
 * Function: Auto_Mode()
 * Description: Full VEML6035 Auto/Self-Timed mode configuration for the
 *  currently selected sensor: sets sensitivity x1, digital gain double,
 *  gain double, 100ms integration time, persistence 1, ALS interrupt
 *  channel/enable, white channel disabled, high/low interrupt thresholds
 *  (10000/8000), disables power-saving mode, powers the sensor on, clears
 *  the initial interrupt and waits 300ms.
 * pramameter: none
 *  return: none
 */
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
/***********************************************************************
 * Function: Power_Saving_Mode()
 * Description: Configures the selected VEML6035 like Auto_Mode() but with
 *  Power Saving Mode enabled (PSM wait 3.2) to reduce sensor power draw.
 *  Sets sensitivity/gain/integration time/persistence/interrupt and
 *  thresholds, powers the sensor on, clears the initial interrupt and
 *  waits 1s.
 * pramameter: none
 *  return: none
 */
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

/***********************************************************************
 * Function: setI2CChannelSeq()
 * Description: Selects the I2C-mux channel mapping for the 5 sensors on
 *  each mux based on the configured PCB version: the {6,0,1,2,3} layout
 *  for boards older than V1.3, or {6,5,1,2,3} for V1.3+, copying the
 *  result into the I2C_Channel lookup table and printing the sequence.
 * pramameter: none
 *  return: none
 */
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

/***********************************************************************
 * Function: ResetAllSensors()
 * Description: Iterates over both I2C multiplexers (5 sensors each) and,
 *  for every one of the 10 VEML6035 sensors, opens its mux channel,
 *  issues a soft Reset_Sensor(), runs Basic_Initialization_Auto_Mode()
 *  and closes the channel again.
 * pramameter: none
 *  return: none
 */
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

/***********************************************************************
 * Function: ChannelEnableProcess_loop()
 * Description: Applies the channel-enable (ALS-only vs ALS&White) setting
 *  to all 10 sensors. For each sensor on both muxes it opens the channel,
 *  reads the current CHANNEL_EN value then sets the required one via
 *  ChannelEnableProcess(), printing per-sensor diagnostics.
 * pramameter: none
 *  return: none
 */
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
/***********************************************************************
 * Function: ChannelEnableProcess()
 * Description: Command handler for the VEML6035 CHANNEL_EN register on the
 *  currently open sensor. Reads/prints the channel-enable state, or sets
 *  it to ALS-only (white disabled) or ALS&White depending on the command
 *  string; prints "Invalid command" otherwise.
 * pramameter: command - control string (ChannelEnableRead /
 *  ChannelEnableSetALS / ChannelEnableSetBoth)
 *  return: none
 */
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

/***********************************************************************
 * Function: ALS_IT_Process_loop()
 * Description: Applies the ALS integration-time setting to all 10
 *  sensors. For each sensor on both muxes it opens the channel, reads the
 *  current ALS_IT then sets the required value via ALS_IT_Process(),
 *  printing per-sensor diagnostics.
 * pramameter: none
 *  return: none
 */
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
/***********************************************************************
 * Function: ALS_IT_Process()
 * Description: Command handler for the VEML6035 ALS integration time on
 *  the currently open sensor. On "ALS_IT" it reads and prints the current
 *  integration time; on "ALS_IT <n>" it maps the numeric index to one of
 *  the 25/50/100/200/400/800 ms settings and applies it, validating the
 *  read-back value.
 * pramameter: command - "ALS_IT" to read or "ALS_IT <index>" to set
 *  return: none
 */
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

/***********************************************************************
 * Function: GainProcess_loop()
 * Description: Applies the analog gain setting to all 10 sensors. For
 *  each sensor on both muxes it opens the channel, reads the current GAIN
 *  then sets the required value via GainProcess(), printing per-sensor
 *  diagnostics.
 * pramameter: none
 *  return: none
 */
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
/***********************************************************************
 * Function: GainProcess()
 * Description: Command handler for the VEML6035 analog GAIN register on
 *  the currently open sensor. On "GAIN" it reads and prints the gain; on
 *  "GAIN Normal"/"GAIN Double" it sets normal or double gain; prints
 *  "Invalid command" otherwise.
 * pramameter: command - "GAIN" to read, or "GAIN Normal"/"GAIN Double"
 *  to set
 *  return: none
 */
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

/***********************************************************************
 * Function: DigitalGainProcess_loop()
 * Description: Applies the digital gain (DG) setting to all 10 sensors.
 *  For each sensor on both muxes it opens the channel, reads the current
 *  DG then sets the required value via DigitalGainProcess(), printing
 *  per-sensor diagnostics.
 * pramameter: none
 *  return: none
 */
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
/***********************************************************************
 * Function: calCalibratedValue()
 * Description: Converts one stored raw sensor sample into a calibrated
 *  fluorescence value by subtracting the channel origin and dividing by
 *  the channel slope (FORTE_ORIGINS / FORTE_SLOPES).
 * pramameter: channel - sensor/slot index (0-9);
 *  cnt - sample/loop index into sensor67Value
 *  return: the calibrated fluorescence value as a float
 */
float sensor6035::calCalibratedValue(int channel, int cnt)
{
    return (float(sensor67Value[channel][cnt]) - FORTE_ORIGINS[channel]) / FORTE_SLOPES[channel];
}
// Function to process digital gain
// Command List:
// DG: Read DG para
// DG Normal: Set DG to Normal
// DG Double: Set DG to Double
/***********************************************************************
 * Function: DigitalGainProcess()
 * Description: Command handler for the VEML6035 digital gain (DG) register
 *  on the currently open sensor. On "DG" it reads and prints the DG; on
 *  "DG Normal"/"DG Double" it sets normal or double digital gain; prints
 *  "Invalid command" otherwise.
 * pramameter: command - "DG" to read, or "DG Normal"/"DG Double" to set
 *  return: none
 */
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

/***********************************************************************
 * Function: SENS_loop()
 * Description: Applies the sensitivity (SENS) setting to all 10 sensors.
 *  For each sensor on both muxes it opens the channel, reads the current
 *  SENS then sets the required value via SENS(), printing per-sensor
 *  diagnostics.
 * pramameter: none
 *  return: none
 */
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
/***********************************************************************
 * Function: SENS()
 * Description: Command handler for the VEML6035 sensitivity (SENS)
 *  register on the currently open sensor. On "SENS" it reads and prints
 *  the sensitivity; on "SENS High"/"SENS Low" it sets high (x1) or low
 *  (1/8x) sensitivity; prints "Invalid command" otherwise.
 * pramameter: command - "SENS" to read, or "SENS High"/"SENS Low" to set
 *  return: none
 */
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

/***********************************************************************
 * Function: Snapshot_loop_test()
 * Description: Startup self-test that sequentially lights each of the 10
 *  LEDs, opens the matching sensor channel and takes a Snapshot() reading.
 *  On repeated read failures (errCnt > 10) it tabulates the error records,
 *  classifies the fault (no data / too dark / too bright), shows the
 *  corresponding error screen and logs it via error.addError(). Switches
 *  off the LED driver when done.
 * pramameter: none
 *  return: none
 */
void sensor6035::Snapshot_loop_test()
{
    // delay(1000);
    // _LED.LED_PWM_Set(LED_PWM_VALUE);        //switch on the LED driver before testing
    for (iChannel = 0; iChannel < 10; iChannel++)
    {
        _LED.LED_on_unguarded(iChannel);
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
                    error.addError(errorLightSensor, errorNoData, sensorStep, iChannel);
                    break;
                case 1:
                    _displayCLD.ErrorProcessatBegin("Too dark\n Please check", String(iChannel + 1));
                    error.addError(errorLightSensor, errorTooDark, sensorStep, iChannel);
                    break;
                case 2:
                    _displayCLD.ErrorProcessatBegin("Too bright\n Please check", String(iChannel + 1));
                    error.addError(errorLightSensor, errorTooBright, sensorStep, iChannel);
                    break;
                default:
                    _displayCLD.ErrorProcessatBegin("Unknown Err\n Please power off/on", String(iChannel + 1));
                    error.addError(errorLightSensor, errorNoData, sensorStep, iChannel);
                    break;
                }
                sleep(3);
                break;
                // errCnt = 1;     //recheck after 3 seconds
            }
        }
        _LED.LED_off_unguarded(iChannel);
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
/***********************************************************************
 * Function: Snapshot()
 * Description: Takes a single ALS reading from the currently open sensor
 *  via VEML6035_GET_ALS_DATA_I2C_Res(). On a comms failure or out-of-range
 *  value (0 or 0xFFFF) it schedules a 100ms re-read, increments errCnt and
 *  bumps the matching errRecord[iChannel] fault counter (comms/too dark/
 *  too bright). On success it clears errCnt and prints the raw and
 *  calibrated value.
 * pramameter: none
 *  return: none
 */
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

/***********************************************************************
 * Function: eSensorPreheat()
 * Description: Non-blocking preheat-step worker. While COUNTER <
 *  PREHEATLOOPS it runs interval-timed rounds, sequentially turning each
 *  of the OPTOCHANNELS LEDs on/off with LED_DELAY_TIME spacing to warm the
 *  optics (no data is stored). After each full round it increments COUNTER
 *  and, once preheat completes (or if PREHEATLOOPS is 0), transitions to
 *  eSensormaintain, and when the heater (phase 2) is also ready updates
 *  the display and sounds the buzzer.
 * pramameter: none
 *  return: none
 */
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
            _LED.LED_on_unguarded(iChannel);
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
                _LED.LED_off_unguarded(iChannel);

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
                    _LED.LED_on_unguarded(iChannel);
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

/***********************************************************************
 * Function: eSensorMaintain()
 * Description: Non-blocking maintain-step worker that keeps the optics
 *  warm after preheat. On each OPTO_INTERVAL it cycles through all
 *  OPTOCHANNELS LEDs on/off with LED_DELAY_TIME spacing (again without
 *  storing data), printing "finish one round maintenance" at the end of
 *  each round, until the run advances to another step.
 * pramameter: none
 *  return: none
 */
void sensor6035::eSensorMaintain()
{
    if ((millis() - START_INTERVAL_TIME) >= OPTO_INTERVAL) // start a new loop
    {
        // Reset interval timing to prepare the next reading
        START_INTERVAL_TIME += OPTO_INTERVAL; // millis();
        // Open LED channel
        iChannel = 0; // start reading from the 1st LED/Sensor
        _LED.LED_on_unguarded(iChannel);
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
            _LED.LED_off_unguarded(iChannel);

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
                _LED.LED_on_unguarded(iChannel);
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

/***********************************************************************
 * Function: outputHeader()
 * Description: Emits the amplification run header. Prints the "<AmpStart>"
 *  tag, then serializes a JSON metadata block (per-channel calibration
 *  slopes/origins and LED power, optical units, device ID, slot count,
 *  amplification time, reading interval and firmware version) followed by
 *  the CSV column header for the fluorescence/temperature log.
 * pramameter: none
 *  return: none
 */
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

/***********************************************************************
 * Function: closeSensorChannel()
 * Description: Closes the I2C-mux channel for the given slot, selecting
 *  the second mux (I2CMux1) for slots 5-9 and the first (I2CMux) for slots
 *  0-4, using the reversed I2C_Channel index mapping.
 * pramameter: slot - sensor/slot index (0-9) whose I2C channel to close
 *  return: none
 */
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

/***********************************************************************
 * Function: openSensorChannel()
 * Description: Opens the I2C-mux channel for the given slot, selecting the
 *  second mux (I2CMux1) for slots 5-9 and the first (I2CMux) for slots
 *  0-4, using the reversed I2C_Channel index mapping, so that sensor can
 *  be addressed over I2C.
 * pramameter: slot - sensor/slot index (0-9) whose I2C channel to open
 *  return: none
 */
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

/***********************************************************************
 * Function: eSensorstartFunc()
 * Description: Entry actions for the eSensorstart step that begin an
 *  amplification measurement run. Turns off all LEDs, closes both muxes,
 *  clears the acquisition controller and the error log/EEPROM, prints the
 *  run header (outputHeader()), sets bSensorReadingFlag to pause hotlid
 *  heating for stable readings, resets COUNTER/interval timer, turns on
 *  the first LED and advances the state machine to eSensor1stReading.
 * pramameter: none
 *  return: none
 */
void sensor6035::eSensorstartFunc()
{
    // switch off all LED first, in case some is still open
    for (uint8_t i = 0; i < 10; i++)
    {
        _LED.LED_off_unguarded(i); // turn off the LED
        // openSensorChannel(i);
        // VEML6035_SET_SD(VEML6035_ALS_SD_OFF);
        // closeSensorChannel(i);
    }

    // reconfigure all sensors in case on lost configuration during
    I2CMux.closeAll();
    I2CMux1.closeAll();

    acquisitionControl.clear();
    error.clear();
    error.clearEEPROM();

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
    _LED.LED_on_unguarded(iChannel); // turn on the 1st LED
    acquisitionControl.clear();
    // closeSensorChannel(iChannel);
    sensor67ValueTime = millis() + LED_DELAY_TIME; // the time that sensor can read
    sensorStep = eSensor1stReading;                // change to the step to start the measurement
    // info_displayln("eSensormaintain");
    tic = millis();
}

/***********************************************************************
 * Function: eSensor1stReadingFunc()
 * Description: Core amplification acquisition worker run during
 *  eSensor1stReading. Each OPTO_INTERVAL it prints the time/counter label
 *  and walks the OPTOCHANNELS sensors: opens the channel, takes repeated
 *  VEML6035 readings via acquisitionControl, and on faults re-configures
 *  the slot (reConfigSingleSlotSensor), logs no-data/too-dark errors and
 *  substitutes fallback values. It averages the repeats into
 *  sensor67Value[channel][COUNTER], prints the calibrated value, and after
 *  each full round increments COUNTER. When MEASUREMENTLOOPS is reached it
 *  saves all data and errors to EEPROM, marks the screen finished, sounds
 *  the buzzer, emits "<AmpStart/>" and returns to eSensormaintain.
 * pramameter: none
 *  return: none
 */
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
            _LED.LED_on_unguarded(iChannel);
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
                }

                if (!acquisitionControl.isFinished())
                {

                    bool flagres;
                    Word sensorResp = 0xFFFF;
                    flagres = VEML6035_GET_ALS_DATA_I2C_Res(&sensorResp);
                    if (sensorResp == 0)
                    {
                        this->reConfigSingleSlotSensor(iChannel);
                        this->openSensorChannel(iChannel);
                        flagres = VEML6035_GET_ALS_DATA_I2C_Res(&sensorResp);
                    }
                    if (!flagres && sensorResp != 0) // if read successfully, store the value
                    {
                        acquisitionControl.store(sensorResp);
                        tic = millis();
                    }
                    else if (flagres || sensorResp <= 2) // if error happened, start the err process
                    {
                        acquisitionControl.addErrorCount();
                    }

                    if (acquisitionControl.isMaxErrorReached())
                    {
                        this->reConfigSingleSlotSensor(iChannel);
                        this->openSensorChannel(iChannel);
                        if (acquisitionControl.getSizeValues() > 0)
                        {
                            acquisitionControl.fixValuesErrors();
                        }
                        else
                        {
                            /* If no existing error found then add a new error */
                            error.addError(
                                errorLightSensor, // errorModule
                                errorNoData,      // errorType
                                sensorStep,       // errorProcessStep
                                iChannel          // errorSlot
                            );

                            for (size_t i = 0; i < acquisitionControl.getRepeats(); i++)
                            {
                                acquisitionControl.store((sensor67Value[iChannel][COUNTER - 1] / 8)); // store zero for the error reading, and continue the process, in case all reading are error
                            }
                        }
                    }

                    /* Error handling for too dark conditions */
                    // if ((acquisitionControl.getSum() <= 5) &&
                    //     (acquisitionControl.getSizeValues() == acquisitionControl.getRepeats())) // if all the reading are zero, which means too dark to read, add error and continue
                    // {
                    //     error.addError(
                    //         errorLightSensor, // errorModule
                    //         errorTooDark,     // errorType
                    //         sensorStep,       // errorProcessStep
                    //         iChannel          // errorSlot
                    //     );
                    //     for (size_t i = 0; i < acquisitionControl.getRepeats(); i++)
                    //     {
                    //         acquisitionControl.store((sensor67Value[iChannel][COUNTER - 1] / 8)); // store zero for the error reading, and continue the process, in case all reading are error
                    //     }
                    // }
                    // add 100 ms to measurement time
                    sensor67ValueTime = millis() + 100;
                }
                else
                {
                    // info_display(">");
                    Word meanResponse = acquisitionControl.getSum();
                    closeSensorChannel(iChannel);
                    acquisitionControl.clear();
                    _LED.LED_off_unguarded(iChannel);
                    // finish one session, clear the err counter
                    errCnt = 0;
                    oddCnt = 0;
                    errRereadTime = 0;
                    // convert the value and output
                    sensor67Value[iChannel][COUNTER] = meanResponse;
                    info_display((float(meanResponse) - FORTE_ORIGINS[iChannel]) / FORTE_SLOPES[iChannel]);
                    info_display(",");

                    iChannel++;
                    if (iChannel < OPTOCHANNELS)
                    {
                        _LED.LED_on_unguarded(iChannel);
                        // On next LED
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
                            // IMPORTANT: persist the amplification record (and error log) to EEPROM
                            // BEFORE telling the display to refresh. DisplayTask runs on the other core
                            // and, the moment changeScreen is set, calls screen_Result() ->
                            // getDataAmplificationEEPROM() which READS this very record back. Signaling
                            // first caused a read-before-write race (screen/upload saw the previous run's
                            // data) plus concurrent access to the shared, unguarded EEPROM object, so the
                            // freshly measured record was effectively never saved.
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

                            error.saveErrorToEEPROM();
                            error.printAllError();

                            // EEPROM (record + errors) is fully committed now — only here is it safe to
                            // let DisplayTask read it back for the result screen and the Google Sheet.
                            // finish the reading, update the step
                            sensorStep = eSensormaintain; // only read during amplification
                            COUNTER = 0;
                            _displayCLD.type_infor = escreenFinished;
                            _displayCLD.bheadershow = true;
                            _displayCLD.changeScreen = true;

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

/***********************************************************************
 * Function: clear()
 * Description: Resets the acquisition state for a fresh run: zeroes
 *  START_INTERVAL_TIME, COUNTER and iChannel, clears the 7-element
 *  SENSOR_DATA buffer and wipes the entire 10 x amplification_time
 *  sensor67Value measurement matrix.
 * pramameter: none
 *  return: none
 */
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

/***********************************************************************
 * Function: switchSensorAcquisitionState()
 * Description: Powers the currently selected VEML6035's ALS acquisition
 *  on or off by writing the shutdown (SD) bit.
 * pramameter: state - true to switch the sensor on (SD_ON), false to
 *  switch it off (SD_OFF)
 *  return: none
 */
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

/***********************************************************************
 * Function: switchAllSensorsAcquisitionState()
 * Description: Switches the ALS acquisition state on or off for all 10
 *  sensors by opening each mux channel in turn, calling
 *  switchSensorAcquisitionState() and closing the channel.
 * pramameter: state - true to power all sensors on, false to power them
 *  off
 *  return: none
 */
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

/***********************************************************************
 * Function: checkSensorConfiguration()
 * Description: Verifies that the currently open VEML6035 still holds the
 *  expected measurement configuration by reading back the ALS integration
 *  time (==6), gain (==1), digital gain (==0), sensitivity (==0) and ALS
 *  mode (==0) register bits.
 * pramameter: none
 *  return: true if every register matches the expected value, false on
 *  the first mismatch
 */
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

/***********************************************************************
 * Function: reConfigSingleSensor()
 * Description: Re-applies the standard measurement configuration to one
 *  sensor: opens its mux channel, writes the CHANNEL_EN, ALS_IT, GAIN, DG
 *  and SENS registers from the Config_* constants, then closes the
 *  channel. Requires all I2C channels to be closed beforehand.
 * pramameter: slot - sensor/slot index (0-9) to reconfigure
 *  return: none
 */
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

/***********************************************************************
 * Function: reConfigSensors()
 * Description: Performs a full re-configuration of all 10 sensors: closes
 *  both muxes, soft-resets every sensor via ResetAllSensors() and then
 *  re-applies the measurement configuration to each slot with
 *  reConfigSingleSensor().
 * pramameter: none
 *  return: none
 */
void sensor6035::reConfigSensors()
{
    // close all channels
    I2CMux.closeAll();
    I2CMux1.closeAll();

    // soft reset channels
    delay(50);
    ResetAllSensors(); // reset all 10 sensors
    delay(100);

    // set configuration
    for (int slot = 0; slot < 10; slot++)
    {
        reConfigSingleSensor(slot);
    }
}

/***********************************************************************
 * Function: connectToSensor()
 * Description: Opens the given slot's I2C channel and validates its
 *  configuration; if checkSensorConfiguration() fails it re-establishes
 *  the settings via reConfigSingleSensor() and re-opens the channel,
 *  leaving the sensor selected and ready to read.
 * pramameter: slot - sensor/slot index (0-9) to connect to
 *  return: none
 */
void sensor6035::connectToSensor(int slot)
{
    openSensorChannel(slot);
    if (!checkSensorConfiguration())
    {
        info_displayln("[W] Re-establishing sensor configuration.");
        reConfigSingleSensor(slot);
        openSensorChannel(slot);
    }
    // VEML6035_SET_ALS_IT(VEML6035_ALS_IT_800ms);
    // VEML6035_SET_SD(VEML6035_ALS_SD_ON);
    delay(10);
}

/***********************************************************************
 * Function: disconnectFromSensor()
 * Description: Powers the currently selected sensor's ALS off (SD_OFF) and
 *  closes the given slot's I2C-mux channel to release the bus.
 * pramameter: slot - sensor/slot index (0-9) to disconnect from
 *  return: none
 */
void sensor6035::disconnectFromSensor(int slot)
{
    VEML6035_SET_SD(VEML6035_ALS_SD_OFF);
    delay(10);
    closeSensorChannel(slot);
}

/***********************************************************************
 * Function: testShot()
 * Description: Manual diagnostic single-slot reading. Turns off all LEDs,
 *  lights the given slot's LED, opens its channel and accumulates repeated
 *  VEML6035 readings via acquisitionControl until finished; on too many
 *  errors it shows the opto-sensor error screen and logs an error. Prints
 *  the summed "Green" response and turns the LED back off.
 * pramameter: slot - sensor/slot index (0-9) to test
 *  return: none
 */
void sensor6035::testShot(int slot)
{

    _LED.LED_OFF_ALL_unguarded();
    // openSensorChannel(slot);
    // VEML6035_SET_SD(VEML6035_ALS_SD_ON);
    _LED.LED_on_unguarded(slot);

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
            error.addError(
                errorLightSensor, // errorModule
                errorNoData,      // errorType
                sensorStep,       // errorProcessStep
                slot              // errorSlot
            );
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
    _LED.LED_off_unguarded(slot);
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

/***********************************************************************
 * Function: OptoCommandProcess()
 * Description: Serial debug command dispatcher for the opto/heater
 *  subsystem. 'R' reconfigures all sensors; 'P' interactively sets an LED
 *  slot's PWM; '0'-'9' run testShot() on that slot; 'A'/'B'/'C' set the
 *  three bottom heater PWMs; 'E'/'F' set the top (hotlid) heater PWMs;
 *  'M' prints the calibration metadata JSON. Several branches block on
 *  Serial input for the new value.
 * pramameter: command - single character selecting the action to perform
 *  return: none
 */
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

/***********************************************************************
 * Function: bSensorReadingGet()
 * Description: Accessor returning the current sensor-reading flag, which
 *  other modules (e.g. the hotlid heater) use to know whether a reading is
 *  in progress and pause heating for measurement stability.
 * pramameter: none
 *  return: the bSensorReadingFlag boolean (true while a reading round is
 *  active)
 */
bool sensor6035::bSensorReadingGet()
{
    return bSensorReadingFlag;
}

//////////////////Calibration
/***********************************************************************
 * Function: calib_sensor()
 * Description: Takes a calibration reading for one slot: turns off all
 *  LEDs, lights the given slot, opens its channel and accumulates repeated
 *  VEML6035 readings via acquisitionControl until finished, then returns
 *  their sum. On too many read errors it shows the error screen, logs a
 *  no-data error and returns -1.
 * pramameter: slot - sensor/slot index (0-9) to read for calibration
 *  return: the summed raw sensor response as a float, or -1 on sensor
 *  error
 */
float sensor6035::calib_sensor(int slot)
{
    _LED.LED_OFF_ALL_unguarded();
    _LED.LED_on_unguarded(slot);

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
            error.addError(
                errorLightSensor, // errorModule
                errorNoData,      // errorType
                sensorStep,       // errorProcessStep
                slot              // errorSlot
            );
            return -1;
        }
        delay(100);
    }
    meanResponse = acquisitionControl.getSum();

    closeSensorChannel(slot);
    _LED.LED_off_unguarded(slot);

    return (float)meanResponse;
}

/***********************************************************************
 * Function: calibration()
 * Description: Drives the multi-point calibration state machine for one
 *  slot. For each of the first 3 calibration points it shows the waiting
 *  screen, stores calib_sensor() into result_calib[type_calib] and
 *  advances type_calib; on the 4th point it takes the final reading, runs
 *  calculate_calib() to fit the line, resets type_calib and shows the
 *  completion screen. Always returns the state machine to eSensorwait.
 * pramameter: slot - sensor/slot index (0-9) being calibrated
 *  return: none
 */
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

/***********************************************************************
 * Function: calculate_calib()
 * Description: Computes a linear calibration fit from the 4 measured
 *  responses against the known concentrations {300,200,100,0}. Performs
 *  least-squares regression and stores the slope in cal_calib[0], the R^2
 *  goodness-of-fit in cal_calib[1] and the intercept/origin in
 *  cal_calib[2].
 * pramameter: y[] - array of 4 measured sensor responses corresponding to
 *  the fixed x concentrations
 *  return: none (results written into the cal_calib[] member array)
 */
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

/***********************************************************************
 * Function: setStepeSensorcalib()
 * Description: Forces the state machine into the eSensorcalib step so the
 *  calibration routine runs on the next loop().
 * pramameter: none
 *  return: none
 */
void sensor6035::setStepeSensorcalib()
{
    sensorStep = eSensorcalib;
}
/***********************************************************************
 * Function: setStepeSensorwait()
 * Description: Forces the state machine into the eSensorwait (idle) step
 *  so loop() performs no sensor work until another step is requested.
 * pramameter: none
 *  return: none
 */
void sensor6035::setStepeSensorwait()
{
    sensorStep = eSensorwait;
}

int I2C_Bus = 3;
sensor6035 _sensor6035;
