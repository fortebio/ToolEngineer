/*
Support parameter configuration by serial communication
To receive the full command, here will wait 10ms after receiving, if there is no further data, then stop to process the data
*/

#include "ForteSetting.h"
#include "Bluetooth.h"
#include "webDashboard.h" // dashboardDeviceBusy(): re-checked before applying web settings

/// @brief Buzzer control
/// "Buzzer", beep one time for 1 seond
/// "Buzzer p1_on p2_off p3_times p4_long_off, p5_long_times";
///     1. config the buzzer to beep p1_on ms;
///     2. stop for p2_off ms
///     3. repeat step 1 and 2 for p3_times times
///     4. stop p4_long_off ms
///     5. repeat step 1 to 4 for p5_long_times times
/// @return true if command is Buzzerxxx, or false
/***********************************************************************
 * Function: BuzzerConfig()
 * Description: Parses a serial "Buzzer" command from recvData. With no
 *  arguments it beeps once for 1000ms; with arguments it splits up to 5
 *  integers (on, off, times, long_off, long_times; defaults {1000,0,1,0,1})
 *  via paraIntSplit and configures/starts the buzzer. Not a JSON/EEPROM key.
 * pramameter: none (reads member recvData/recvLen)
 *  return: true if recvData starts with "Buzzer", false otherwise
 */
bool ForteSetting::BuzzerConfig()
{
    if (strncasecmp(recvData, "Buzzer", 6))
    { //-1 means it's not buzzer command
        return false;
    }
    if (recvLen < 7) // only Buzzer command, then beep one time for 1 seconds
    {
        _buzzer.BuzzerSet(1000, 0, 1);
        _buzzer.BuzzerStart();
        return true;
    }
    int para[] = {1000, 0, 1, 0, 1}; // default para
    paraIntSplit(recvData + 7, para);
    _buzzer.BuzzerConfig(para);
    _buzzer.BuzzerStart();
    return true;
}

/// @brief Fan on/off control
/// "Fan On": turn on the Fan
/// "Fan...": other will turn off the Fan
/// @return true if command is "Fan..."", or return false
/***********************************************************************
 * Function: FanConfig()
 * Description: Parses a serial "Fan" command from recvData. "Fan On"
 *  starts the cooling fan; any other "Fan..." string stops it. Runtime
 *  control only, not a JSON/EEPROM parameter.
 * pramameter: none (reads member recvData)
 *  return: true if recvData starts with "Fan", false otherwise
 */
bool ForteSetting::FanConfig()
{
    if (strncasecmp(recvData, "Fan", 3))
    { //-1 means it's not Fan command
        return false;
    }
    if (!strncasecmp(recvData, "Fan On", 6))
    {
        _Fan.FanStart();
        return true;
    }
    _Fan.FanStop();
    return true;
}

/// @brief Active the heater simulation by time, not from thermometer, will deactive all heater
/// "HeaterSimulate" will stop all heating and simulate the temperature of all heaters by time increase
/// @return true if command is "HeaterSimulate..."
/***********************************************************************
 * Function: HeaterSimuConfig()
 * Description: Parses a serial "HeaterSimulate" command from recvData.
 *  Stops all heating then enables time-based temperature simulation for
 *  all heaters (heatSimulation(0xFF)) instead of using thermometer
 *  readings. Runtime debug control, not a JSON/EEPROM parameter.
 * pramameter: none (reads member recvData)
 *  return: true if recvData starts with "HeaterSimulate", false otherwise
 */
bool ForteSetting::HeaterSimuConfig()
{
    if (strncasecmp(recvData, "HeaterSimulate", 14))
    { //-1 means it's not heater simulation command
        return false;
    }
    _PIDControl.stopAllHeating();
    info_displayln("Heater Simulation");
    _PIDControl.heatSimulation(0xFF);
    return true;
}

/// @brief "TemperatureOutput" will invert temperature output, to start or stop
/// @return true if command is "TemperatureOutput"
/***********************************************************************
 * Function: TemperatureOutput()
 * Description: Parses a serial "TemperatureOutput" command from recvData
 *  and toggles (inverts) continuous temperature output streaming via
 *  _PIDControl.RevTemperatureOutput(). Runtime control, not a JSON/EEPROM
 *  parameter.
 * pramameter: none (reads member recvData)
 *  return: true if recvData starts with "TemperatureOutput", false otherwise
 */
bool ForteSetting::TemperatureOutput()
{
    if (strncasecmp(recvData, "TemperatureOutput", 17))
    { //-1 means it's not temperature output command
        return false;
    }
    info_displayln("Revise temperature output now");
    _PIDControl.RevTemperatureOutput();
    return true;
}

/// @brief set at different step to skip certain period
/// "StepSet Amp": Skip lysis and start the Amplification stage
/// "StepSet Measure": Skip lysis and amplification preheat with heater simulation, start opto reading directly
/// @return true if command is "StepSet ...", or return false
/***********************************************************************
 * Function: HeaterStepSet()
 * Description: Parses a serial "StepSet" command from recvData to skip
 *  to a later run stage. "StepSet Amp" skips lysis and begins amplification
 *  preheat (setPreheat67, sensor preheat, epreheating67 screen). "StepSet
 *  Measure" enables heater simulation and jumps to amplification measurement
 *  (setPID23Ready, ewaitampTube screen). Runtime control, not JSON/EEPROM.
 * pramameter: none (reads member recvData)
 *  return: true if recvData starts with "StepSet", false otherwise
 */
bool ForteSetting::HeaterStepSet()
{
    if (strncasecmp(recvData, "StepSet", 7))
    {
        return false;
    }
    if (!strncasecmp(recvData + 8, "Amp", 3))
    {
        info_displayln("Skip to start the Amplification preheating directly");
        _PIDControl.setPreheat67();
        _PIDControl.timeStartWait = millis(); // seed the hotlid 15-min wait origin (like the button paths)
        _sensor6035.setStepeSensorpreheat();
        _displayCLD.type_infor = eheating67; // updated to start preheat directly in 26 Feb, 2024
        _displayCLD.bheadershow = true;
        _displayCLD.changeScreen = true;

        return true;
    }
    if (!strncasecmp(recvData + 8, "Measure", 7))
    {
        info_displayln("Skip to start the Amplification measurement directly");
        _PIDControl.heatSimulation(0xFF);
        _PIDControl.setPID23Ready();
        _displayCLD.type_infor = ewaitampTube;
        _displayCLD.bheadershow = true;
        _displayCLD.changeScreen = true;

        return true;
    }
    return true;
}

// Copy a JSON array into a fixed-size parameter array, logging each element. Replaces
// ~11 identical containsKey+for+cast+store+log blocks in JsonDataConfig(). The array is
// taken BY REFERENCE (T (&dst)[N]) so both the element type T and the capacity N are
// deduced from parameter.* itself - and `i < N` clamps to that capacity, so an
// over-long JSON array can no longer write past the struct field (the heater/setpoint
// arrays sit next to each other - CLAUDE.md GOTCHA 4). Valid-length input is unchanged.
template <typename T, size_t N>
static void loadJsonArr(JsonArray src, T (&dst)[N], const char *label)
{
    info_displayln(label);
    for (size_t i = 0; i < src.size() && i < N; i++)
    {
        dst[i] = src[i].as<T>();
        info_displayln(dst[i]);
    }
}

/// @brief to analyze the json data with parameter inside, then write into EEPROM
/// input the right whole json data directly, then it will get all elements and write into EEPROM
/// @return return true and all parameter data if it's correct, or return false
/***********************************************************************
 * Function: JsonDataConfig()
 * Description: Parses a full JSON document received in recvData (commands
 *  starting with '{'). When key "para version" is present, deserializes the
 *  whole device configuration into the parameter struct: para/PCB version,
 *  opto calibration slopes/origins, LED power, analysis parameters (min
 *  increase/sharpness/slight positive time, detect shape, detection margin,
 *  arm/transition percentile, sg order/window, baseline start/range), units,
 *  device ID, lysis/opto-preheat/LED durations, time per loop, amplification
 *  time, lysis/amplification temperatures, bottom/top sensor sequences, PID/
 *  PID2/PID3 params, bottom/top overheat values, temperature offsets, top
 *  heater PWM high/low pairs, buzzer mode (On=1/Off=0/PID=2), kitId, empty
 *  array, and a non-persisted "counter" display flag. Sets parameter.length
 *  and writes the struct to EEPROM at PARAMETERPOS. A "raw_data" key instead
 *  runs the algorithm test loop (AlgLoop).
 * pramameter: none (reads member recvData)
 *  return: true if recvData starts with '{' and is handled (or unsupported
 *   JSON); false if not JSON or on parse error
 */
bool ForteSetting::JsonDataConfig()
{
    if (strncasecmp(recvData, "{", 1))
    {
        return false;
    }
    info_display("Json data received\n");
    // Parse the received JSON data
    DynamicJsonDocument json_document(1024 * 3);
    DeserializationError error = deserializeJson(json_document, recvData);
    if (error)
    {
        info_display("Error parsing JSON: ");
        info_displayln(error.c_str());
        return false;
    }
    else
    {
        // Access JSON data
        if (json_document.containsKey("para version"))
        {
            info_displayln("parameter configuration");
            if (sizeof(parameter) > 512 - 110)
            {
                info_displayln("Parameter is too long");
                return true;
            }

            // if (json_document.containsKey("para version"))
            {
                String paraVersion = json_document["para version"].as<String>();
                strcpy(parameter.para_version, paraVersion.c_str());
                info_displayln("Para Version: " + paraVersion);
            }

            if (json_document.containsKey("PCB version"))
            {
                String PCBVersion = json_document["PCB version"].as<String>();
                strcpy(parameter.PCB_version, PCBVersion.c_str());
                info_displayln("PCB Version: " + PCBVersion);
            }

            // Print the extracted data
            if (json_document.containsKey("opto calibration"))
            {
                JsonObject calibration = json_document["opto calibration"];
                loadJsonArr(calibration["slopes"].as<JsonArray>(), parameter.slopes, "Calibration - Slopes:");
                loadJsonArr(calibration["origins"].as<JsonArray>(), parameter.origins, "Calibration - Origins:");
            }

            if (json_document.containsKey("LED power"))
                loadJsonArr(json_document["LED power"].as<JsonArray>(), parameter.led_power, "LED Power:");

            // extract the parameter
            if (json_document.containsKey("parameters"))
            {
                JsonObject json_object = json_document["parameters"];
                if (json_object.containsKey("min increase"))
                {
                    double min_inc = json_object["min increase"];
                    parameter.min_increase = min_inc;
                    info_displayln("min increase: " + String(min_inc));
                }

                if (json_object.containsKey("min sharpness"))
                {
                    double min_shp = json_object["min sharpness"];
                    parameter.min_sharpness = min_shp;
                    info_displayln("min sharpness: " + String(min_shp));
                }

                if (json_object.containsKey("min slight positive time"))
                {
                    double min_spt = json_object["min slight positive time"];
                    parameter.min_slight_positive_time = min_spt;
                    info_displayln("min slight positive time: " + String(min_spt));
                }

                if (json_object.containsKey("detect shape"))
                {
                    bool detect_shp = json_object["detect shape"];
                    parameter.detect_shape = detect_shp;
                    info_displayln("detect shape: " + String(detect_shp));
                }

                if (json_object.containsKey("detection margin time"))
                {
                    double value = json_object["detection margin time"];
                    parameter.detection_margin_time = value;
                    info_displayln("detection margin time: " + String(value));
                }

                if (json_object.containsKey("arm percentile"))
                {
                    double value = json_object["arm percentile"];
                    parameter.arm_percentile = value;
                    info_displayln("arm percentile: " + String(value));
                }

                if (json_object.containsKey("transition percentile"))
                {
                    double value = json_object["transition percentile"];
                    parameter.transition_percentile = value;
                    info_displayln("transition percentile: " + String(value));
                }

                if (json_object.containsKey("sg order"))
                {
                    uint8_t value = json_object["sg order"];
                    parameter.sg_order = value;
                    info_displayln("sg order: " + String(value));
                }

                if (json_object.containsKey("sg window"))
                {
                    uint8_t value = json_object["sg window"];
                    parameter.sg_window = value;
                    info_displayln("sg window: " + String(value));
                }

                if (json_object.containsKey("baseline start"))
                {
                    uint8_t value = json_object["baseline start"];
                    parameter.baseline_start = value;
                    info_displayln("baseline start: " + String(value));
                }

                if (json_object.containsKey("baseline range"))
                {
                    uint8_t value = json_object["baseline range"];
                    parameter.baseline_range = value;
                    info_displayln("baseline range: " + String(value));
                }
            }

            if (json_document.containsKey("units"))
            {
                String units = json_document["units"].as<String>();
                strcpy(parameter.units, units.c_str());
                info_displayln("Units: " + units);
            }

            if (json_document.containsKey("device ID"))
            {
                String deviceId = json_document["device ID"].as<String>();
                strcpy(parameter.device_id, deviceId.c_str());
                info_displayln("Device ID: " + deviceId);
            }

            if (json_document.containsKey("lysis duration"))
            {
                uint16_t lysisDuration = json_document["lysis duration"];
                // parameter.LYSIS_DURATION = lysisDuration;
                parameter.lysisDuration = lysisDuration;
                info_displayln("Lysis duration: " + String(lysisDuration));
            }

            if (json_document.containsKey("opto preheat time"))
            {
                uint16_t optopreheatduraton = json_document["opto preheat time"];
                parameter.optopreheatduration = optopreheatduraton;
                info_displayln("opto preheat time: " + String(optopreheatduraton));
            }

            if (json_document.containsKey("LED Duration"))
            {
                uint LEDDuration = json_document["LED Duration"];
                parameter.LEDDuration = LEDDuration;
                info_displayln("LED Duration: " + String(LEDDuration));
            }

            if (json_document.containsKey("time per loop"))
            {
                ulong loopDuration = json_document["time per loop"];
                parameter.timePerLoop = loopDuration;
                info_displayln("time per loop: " + String(loopDuration));
            }

            if (json_document.containsKey("amplification time"))
            {
                int amplificationTime = json_document["amplification time"];
                parameter.amplification_time = amplificationTime;
                info_displayln("Amplification time: " + String(amplificationTime));
            }

            if (json_document.containsKey("lysis temperature"))
            {
                float lysisTemp = json_document["lysis temperature"];
                parameter.lysisTemp = lysisTemp;
                info_displayln("lysis temperature: " + String(lysisTemp));
            }

            if (json_document.containsKey("amplification temperature"))
            {
                float ampTemp = json_document["amplification temperature"];
                parameter.amplifTemp = ampTemp;
                info_displayln("amplification temperature: " + String(ampTemp));
            }

            if (json_document.containsKey("bottom temperature sensor seq"))
                loadJsonArr(json_document["bottom temperature sensor seq"].as<JsonArray>(), parameter.bottomTemperatureSensorSq, "bottom temperature sensor seq:");

            if (json_document.containsKey("top temperature sensor seq"))
                loadJsonArr(json_document["top temperature sensor seq"].as<JsonArray>(), parameter.topTemperatureSensorSq, "top temperature sensor seq:");

            if (json_document.containsKey("PID parameter"))
                loadJsonArr(json_document["PID parameter"].as<JsonArray>(), parameter.kpid, "PID parameter of bottom heater1:");

            if (json_document.containsKey("PID2 parameter"))
                loadJsonArr(json_document["PID2 parameter"].as<JsonArray>(), parameter.kpid2, "PID2 parameter of bottom heater2&3:");

            if (json_document.containsKey("PID3 parameter"))
                loadJsonArr(json_document["PID3 parameter"].as<JsonArray>(), parameter.kpid3, "PID3 parameter of top hotlid2&3:");

            if (json_document.containsKey("Bottom overheat value"))
                loadJsonArr(json_document["Bottom overheat value"].as<JsonArray>(), parameter.bottomOverheat, "Bottom overheat value:");

            if (json_document.containsKey("Top overheat value"))
                loadJsonArr(json_document["Top overheat value"].as<JsonArray>(), parameter.topOverheat, "Top overheat value:");

            if (json_document.containsKey("temperature value calibration"))
                loadJsonArr(json_document["temperature value calibration"].as<JsonArray>(), parameter.temperatureOffset, "temperature value calibration");

            if (json_document.containsKey("top heater PWM"))
            {
                JsonArray hotlidPWM = json_document["top heater PWM"];
                info_displayln("top heater PWM");
                for (uint8_t i = 0; i < hotlidPWM.size(); i++)
                {
                    JsonArray pwmvalue = hotlidPWM[i];
                    uint8_t high = uint8_t(pwmvalue[0]);
                    uint8_t low = uint8_t(pwmvalue[1]);
                    // float f = float(sensorOffset[i]);
                    parameter.hotlidPWM[i][0] = high;
                    parameter.hotlidPWM[i][1] = low;
                    info_displayf("%d:%d\n", high, low);
                }
            }

            if (json_document.containsKey("buzzer"))
            {
                String buzzerOn = json_document["buzzer"].as<String>();
                info_display("buzzer: ");
                if (strncasecmp(buzzerOn.c_str(), "On", 2))
                {
                    parameter.buzzerOn = 0; // Off
                    info_displayln("Off");
                    if (strncasecmp(buzzerOn.c_str(), "PID", 3) == 0)
                    {
                        parameter.buzzerOn = 2; // Special used for PID debug
                    }
                }
                else
                {
                    parameter.buzzerOn = 1; // On
                    info_displayln("On");
                }
            }

            if (json_document.containsKey("kitId"))
            {
                double kitId = json_document["kitId"];
                parameter.kitId = kitId;
                info_displayln("kitId: " + String(kitId));
            }

            if (json_document.containsKey("empty"))
            {
                JsonArray empty = json_document["empty"];
                info_displayln("empty: ");
                for (uint8_t i = 0; i < empty.size(); i++)
                {
                    double tmp = double(empty[i]);
                    parameter.empty[i] = tmp;
                    info_displayln(tmp)
                }
            }

            if (json_document.containsKey("counter")) // for test purpose only, to show the diagram better, it won't be saved in the EEPROM
            {
                _sensor6035.setCounterDisplayflag(true);
            }
            else
            {
                _sensor6035.setCounterDisplayflag(false);
            }

            parameter.length = sizeof(parameter); // use this to indicate the EEPROM has valid parameter
            EEPROM.begin(_EEPROM_SIZE);
            EEPROM.put(PARAMETERPOS, parameter);
            EEPROM.commit();
            EEPROM.end();
            return true;
        }
        else if (json_document.containsKey("raw_data")) // include raw data which means for the testing purpose
        {
            info_displayln("algorithm testing");
            _sensor6035.AlgLoop(recvData);
        }

        else
        {
            info_display("Not supported json data\n");
            return true;
        }
    }
    return true;
}

/// @brief read the parameter stored in the EEPROM
/// "ParaRead" will return all parameter get from EEPROM with its key value
/// @return
/***********************************************************************
 * Function: ParaRead()
 * Description: Parses a serial "ParaRead" command from recvData and calls
 *  loadParaFromEEPROM() to read the stored parameter struct from EEPROM and
 *  echo all parameters back with their key values.
 * pramameter: none (reads member recvData)
 *  return: true if recvData starts with "ParaRead", false otherwise
 */
bool ForteSetting::ParaRead()
{
    if (strncasecmp(recvData, "ParaRead", 8))
    {
        return false;
    }
    loadParaFromEEPROM();
    return true;
}

/// @brief to read all the data in the EEPROM 0~4095
/// "EEPROMRead" will read all the EEPROM data and response with 64 hexadecimal number(32 bytes)
/// @return true if the right command with reading data. or return false
/***********************************************************************
 * Function: EEPROMRead()
 * Description: Parses a serial "EEPROMRead" command from recvData and calls
 *  readEEPROM() to dump the entire EEPROM contents (0~4095) back over the
 *  link as hexadecimal output.
 * pramameter: none (reads member recvData)
 *  return: true if recvData starts with "EEPROMRead", false otherwise
 */
bool ForteSetting::EEPROMRead()
{
    if (strncasecmp(recvData, "EEPROMRead", 10))
    {
        return false;
    }
    readEEPROM();
    return true;
}

/// @brief read the old record and generate output
/// "getResult" will read all old data stored in the EEPROM, then calculate it again to form the output
/// @return true if right command with organised data, or it return false
/***********************************************************************
 * Function: resultOutput()
 * Description: Parses a serial "getResult" command from recvData. Reads the
 *  stored raw sensor record (10x130 Words) from EEPROM at RECORDPOS into
 *  _sensor6035.sensor67Value, then switches the display to the result-review
 *  screen (escreenReview) so the old run is recalculated and shown.
 * pramameter: none (reads member recvData)
 *  return: true if recvData starts with "getResult", false otherwise
 */
bool ForteSetting::resultOutput()
{
    if (strncasecmp(recvData, "getResult", 12))
    {
        return false;
    }
    EEPROM.begin(_EEPROM_SIZE);
    Word tmp[10 * 130] = {0};
    EEPROM.get(RECORDPOS, tmp);

    memcpy(_sensor6035.sensor67Value, tmp, sizeof(tmp));

    EEPROM.end();
    _displayCLD.changeScreen = true;
    _displayCLD.type_infor = escreenReview;
    return true;
}

/// @brief Restart the system
/// "Res" will restart the devicex
/// @return System restart
/***********************************************************************
 * Function: restart()
 * Description: Parses a serial "Res" command from recvData, shows a restart
 *  notice on the display, waits 1s, then reboots the ESP32 via ESP.restart().
 *  Runtime command, not a JSON/EEPROM parameter.
 * pramameter: none (reads member recvData)
 *  return: true if recvData starts with "Res" (device reboots before return);
 *   false otherwise
 */
bool ForteSetting::restart()
{
    if (strncasecmp(recvData, "Res", 3))
    {
        return false;
    }
    info_displayln("\n\nRestart the device now")
        _displayCLD.RestartProcess("Restart now", "As requested");
    delay(1000);
    ESP.restart();
    return true;
}

/***********************************************************************
 * Function: start_amplification_simulation()
 * Description: Parses a JSON command (recvData starting with '{') whose last
 *  character is a slot digit 0-9. Loads existing amplification data from
 *  EEPROM (getDataAmplificationEEPROM), then copies the JSON "Slot" array
 *  (amplification_time samples) into _sensor6035.sensor67Value[slot], and
 *  writes the full 10x130 Word record back to EEPROM at RECORDPOS. Used to
 *  inject simulated amplification curves for one slot.
 * pramameter: none (reads/modifies members recvData and recvLen; slot taken
 *  from last char of recvData)
 *  return: true on successful parse and EEPROM write; false if not JSON, no
 *   trailing slot digit, or JSON parse error
 */
bool ForteSetting::start_amplification_simulation()
{
    uint8_t slot;
    uint8_t loops = this->parameter.amplification_time;
    if (strncasecmp(recvData, "{", 1))
    {
        return false;
    }

    if (recvData[recvLen - 1] >= '0' && recvData[recvLen - 1] <= '9')
    {
        slot = recvData[recvLen - 1] - '0';
        recvData[recvLen - 1] = '\0';
        recvLen--;
    }
    else
    {
        return false;
    }

    getDataAmplificationEEPROM();

    info_display("Json data received\n");
    DynamicJsonDocument json_document(1024 * 3);
    DeserializationError error = deserializeJson(json_document, recvData);
    if (error)
    {
        info_display("Error parsing JSON: ");
        info_displayln(error.c_str());
        return false;
    }
    else
    {
        if (json_document.containsKey("Slot"))
        {
            for (size_t i = 0; i < loops; i++)
            {
                _sensor6035.sensor67Value[slot][i] = json_document["Slot"][i];
            }
        }
    }
    EEPROM.begin(_EEPROM_SIZE);
    Word tmp[10 * 130] = {0};
    memcpy(tmp, _sensor6035.sensor67Value, sizeof(tmp));
    EEPROM.put(RECORDPOS, tmp);
    delay(100);
    EEPROM.commit();
    EEPROM.end();

    return true;
}

/***********************************************************************
 * Function: paraIntSplit()
 * Description: Tokenizes a space-separated string in place (strtok) and
 *  converts each token to an int via atoi, storing results sequentially into
 *  the para array. Used to parse numeric command arguments (e.g. buzzer
 *  timing). Does not bound-check para against the token count.
 * pramameter: source - mutable C string of space-separated integers (modified
 *  by strtok); para - output int array receiving the parsed values
 *  return: number of integers parsed and written into para
 */
int ForteSetting::paraIntSplit(char *source, int *para)
{
    char *token = strtok(source, " ");
    int i = 0;
    while (token != NULL)
    {
        /* code */
        // info_displayln(token);
        para[i] = atoi(token);
        i++;
        token = strtok(NULL, " ");
    }
    return i;
}

/***********************************************************************
 * Function: ForteSetting()
 * Description: Constructor for the ForteSetting class. Empty body; no
 *  configuration is loaded here (see begin() for EEPROM parameter loading).
 * pramameter: none
 *  return: none
 */
ForteSetting::ForteSetting(/* args */)
{
}

/***********************************************************************
 * Function: ~ForteSetting()
 * Description: Destructor for the ForteSetting class. Empty body; no cleanup
 *  required.
 * pramameter: none
 *  return: none
 */
ForteSetting::~ForteSetting()
{
}

/***********************************************************************
 * Function: begin()
 * Description: Initialization routine. Reads the parameter struct from EEPROM
 *  at PARAMETERPOS and validates it by comparing the stored length field
 *  against sizeof(parameter). If valid, copies it into the active parameter
 *  member and displays it (paraDisplay); otherwise keeps the compiled-in
 *  defaults and shows an error prompting the user to initialize parameters.
 * pramameter: none
 *  return: none
 */
void ForteSetting::begin()
{
    parastructure paraEEPROM;
    EEPROM.begin(_EEPROM_SIZE);
    EEPROM.get(PARAMETERPOS, paraEEPROM);
    info_displayf("check para in EEPROM, length is %d\n", paraEEPROM.length);
    if (paraEEPROM.length == sizeof(parameter)) // if the length of the parameter in EEPROM is not -1 or 0, then use it.
    {
        info_displayf("there is para in the EEPROM with length %d\n", sizeof(parameter));
        parameter = paraEEPROM; // keep the user's saved configuration across reboots

        // One-time migration: configs saved before kpid3 existed have kpid3 == {0,0,0}.
        // Seed the defaults and persist so it sticks. Done ONLY inside the valid-config
        // branch so we never write back garbage (e.g. on a fresh/erased EEPROM).
        if (FirmwareVer == "v2.4.2" &&
            parameter.kpid3[0] == 0 && parameter.kpid3[1] == 0 && parameter.kpid3[2] == 0)
        {
            parameter.kpid3[0] = 60;
            parameter.kpid3[1] = 0.1;
            parameter.kpid3[2] = 40;
            parameter.length = sizeof(parameter);
            EEPROM.put(PARAMETERPOS, parameter);
            EEPROM.commit();
            info_displayln("kpid3 seeded to defaults and saved");
        }
        paraDisplay(parameter);
    }
    else
    {
        // Fresh / erased EEPROM: run on compiled defaults but DON'T persist them here,
        // so the device keeps prompting until a real config is saved (each save path
        // writes parameter.length = sizeof, which is what makes it persist on reboot).
        info_displayln("there is no para in the EEPROM");
        _displayCLD.ErrorDisplay("No prarmeter in the EEPROM, please initialize it, default parameter is used now");
        delay(3000);
    }
    // protoID = parameter.device_id;//"proto1";
    // OpticalUnits = parameter.units;//"counts";
    EEPROM.end();
}

/// @brief loop to receive the command from serial port and BT
/// All the supported commands are list as below:
/// "Buzzer", beep one time for 1 seond
/// "Buzzer p1_on p2_off p3_times p4_long_off, p5_long_times";
///     1. config the buzzer to beep p1_on ms;
///     2. stop for p2_off ms
///     3. repeat step 1 and 2 for p3_times times
///     4. stop p4_long_off ms
///     5. repeat step 1 to 4 for p5_long_times times
///
/// "Fan On": turn on the Fan
/// "Fan...": other will turn off the Fan
///
/// "HeaterSimulate" will stop all heating and simulate the temperature of all heaters by time increase
///
/// TemperatureOutput"TemperatureOutput" will invert temperature output, to start or stop
///
/// "StepSet Amp": Skip lysis and start the Amplification stage
/// "StepSet Measure": Skip lysis and amplification preheat with heater simulation, start opto reading directly
///
///{...}@: Json data directly via serial or BT. As
/// to analyze the json data with parameter inside, then write into EEPROM
/// input the right whole json data directly, then it will get all elements and write into EEPROM
///
/// "ParaRead" will return all parameter get from EEPROM with its key value
///
/// "EEPROMRead" will read all the EEPROM data and response with 64 hexadecimal number(32 bytes)
///
/// "getResult" will read all old data stored in the EEPROM, then calculate it again to form the output
///
/// "Res" will restart the device
/***********************************************************************
 * Function: loop()
 * Description: Main command-receive loop. Reads incoming bytes from the USB
 *  Serial port (or Bluetooth SerialBT when not released) into recvData,
 *  handling long JSON payloads terminated by '@' or '#' with a multi-receive
 *  timeout window. A single byte is forwarded to the opto command handler;
 *  longer commands are dispatched in priority order through BuzzerConfig,
 *  FanConfig, HeaterSimuConfig, TemperatureOutput, HeaterStepSet,
 *  start_amplification_simulation, JsonDataConfig, ParaRead, EEPROMRead,
 *  resultOutput and restart, logging "Command is not supported!" if none match.
 * pramameter: none (reads/writes members recvData, recvLen, recvTime, moreMsg)
 *  return: none
 */
/***********************************************************************
 * Function: postConfigJson() / postWifiCreds() / postDeviceId()
 * Description: Queue a settings change coming from the web. Called on the
 *  AsyncTCP task: they ONLY copy the strings and raise the flag (written last),
 *  never touching `parameter` or EEPROM. drainPending() does the real work on
 *  SettingTask. Reject while a request is still pending so a burst of POSTs
 *  cannot clobber an unapplied one.
 * pramameter: the payload
 *  return: false if another request is still queued
 */
bool ForteSetting::postConfigJson(const String &json)
{
    if (pendingKind != PEND_NONE)
        return false;
    pendingA = json;
    cfgSeq++;
    cfgState = CFG_PENDING;    // set BEFORE the flag: once pendingKind is published,
    __sync_synchronize();      // release: payload+state visible before the flag (2 cores)
    pendingKind = PEND_CONFIG; // SettingTask may drain and set CFG_APPLIED at once,
    return true;               // and a later CFG_PENDING here would clobber it.
}

bool ForteSetting::postWifiCreds(const String &ssid_, const String &pass_)
{
    if (pendingKind != PEND_NONE)
        return false;
    pendingA = ssid_;
    pendingB = pass_;
    cfgSeq++;
    cfgState = CFG_PENDING; // before the flag - see postConfigJson()
    __sync_synchronize();
    pendingKind = PEND_WIFI;
    return true;
}

bool ForteSetting::postDeviceId(const String &id)
{
    if (pendingKind != PEND_NONE)
        return false;
    pendingA = id;
    cfgSeq++;
    cfgState = CFG_PENDING; // before the flag - see postConfigJson()
    __sync_synchronize();
    pendingKind = PEND_ID;
    return true;
}

bool ForteSetting::postReviewLast()
{
    if (pendingKind != PEND_NONE)
        return false;
    __sync_synchronize(); // no payload; publish the flag last
    pendingKind = PEND_REVIEW;
    return true;
}

/***********************************************************************
 * Function: drainPending()
 * Description: Apply a web-queued settings change. Runs on SettingTask, so it
 *  is the ONLY task doing EEPROM writes for settings (JsonDataConfig() and
 *  saveSettingDevice() each do their own EEPROM.begin/end - overlapping them
 *  from two tasks would free the shared 4096-byte buffer under the other).
 *
 *  Re-checks dashboardDeviceBusy() HERE, not just in the web handler: the check
 *  in the handler is a TOCTOU (the user can start a run between the POST and
 *  this drain). Checking and applying in the same task closes that window.
 * pramameter: none
 *  return: none
 */
void ForteSetting::drainPending()
{
    // Deferred reboot after a WiFi save: the HTTP response must go out first.
    if (restartAt && millis() >= restartAt)
    {
        info_displayln("[cfg] restarting to apply WiFi");
        delay(50);
        ESP.restart();
    }

    if (pendingKind == PEND_NONE)
        return;
    __sync_synchronize(); // acquire: pair with the release in post*() so pendingA/B
                          // are fully visible on this core before we read them

    e_pending kind = pendingKind;

    // Never apply settings while the device is running / calibrating / uploading.
    if (dashboardDeviceBusy())
    {
        // Dropped on purpose: applying mid-run would change setpoints under a live
        // sample. Publish it so the web can say so instead of reporting "Saved" -
        // the POST already ACKed before this check could run (TOCTOU).
        Serial.println("[cfg] device busy - queued settings dropped");
        cfgState = CFG_BUSY;
        pendingKind = PEND_NONE;
        return;
    }

    if (kind == PEND_CONFIG)
    {
        // Feed the SAME parser the Serial path uses. It reads recvData only, and it
        // applies just the keys present (every field is containsKey-guarded), so a
        // per-card subset merges onto the current values and is then persisted.
        strlcpy(recvData, pendingA.c_str(), sizeof(recvData));
        recvLen = strlen(recvData);
        JsonDataConfig();
        recvLen = 0;
        recvData[0] = '\0';
    }
    else if (kind == PEND_WIFI)
    {
        ssid = pendingA;
        password = pendingB;
        saveSettingDevice();
        info_displayln("[cfg] WiFi saved: " + ssid);
        // Cannot connect in place: the radio is shared, so associating to a router on
        // another channel drops every SoftAP client (including the browser that just
        // posted this). Reboot instead and let setup()'s WiFi.begin + AP fallback run.
        restartAt = millis() + 1500;
    }
    else if (kind == PEND_ID)
    {
        // Two separate stores: the global id_device (EEPROM ADDR_ID_DEVICE_BASE, what
        // the dashboard and the Google Sheet upload use) and parameter.device_id
        // (PARAMETERPOS). Keep them in sync or the web would show one and upload another.
        id_device = pendingA;
        saveSettingDevice();
        strlcpy(parameter.device_id, pendingA.c_str(), sizeof(parameter.device_id));
        parameter.length = sizeof(parameter);
        EEPROM.begin(_EEPROM_SIZE);
        EEPROM.put(PARAMETERPOS, parameter);
        EEPROM.commit();
        EEPROM.end();
        info_displayln("[cfg] device id: " + id_device);
    }
    else if (kind == PEND_REVIEW)
    {
        // Re-load the last completed run from EEPROM and recompute its results, so the
        // web Result tab can review it after a reboot (the RAM cache - gResultsReady,
        // lastRunLoops - is gone by then, but the raw record persists at RECORDPOS,
        // written by sensor6035 when the run finished). Guarded to idle above, so the
        // device is not reading the sensor into sensor67Value while we overwrite it.
        getDataAmplificationEEPROM(); // RECORDPOS -> _sensor6035.sensor67Value (RAW)
        // Uninitialised EEPROM reads as 0xFFFF; a real run's raw baseline is ~150-260.
        // Skip if there is no plausible stored run, so a fresh device shows nothing
        // rather than garbage.
        uint16_t probe = _sensor6035.sensor67Value[0][0];
        if (probe > 10 && probe < 60000)
        {
            float ct[10] = {0};
            char res[10] = {0};
            _sensor6035.bResultGet(ct, res);                           // recompute CT / P-N-S
            dashboardSetResults(ct, res);                              // cache for GET /slots
            _sensor6035.setLastRunLoops(parameter.amplification_time); // GET /curve length
            info_displayln("[review] reloaded last run from EEPROM");
        }
        // else: no plausible stored run -> leave the cache empty (Result tab shows nothing).
    }

    cfgState = CFG_APPLIED; // written to EEPROM; the web can now trust a read-back
    pendingKind = PEND_NONE;
}

bool ForteSetting::readCommand(Stream &port, unsigned long window)
{
    recvLen = 0;
    // Reset moreMsg too: it is a member, and the partial message (recvLen) is wiped here.
    // A previous call that timed out mid multi-chunk left moreMsg=true; without this reset
    // the next fresh command would be misparsed as a continuation of the abandoned one.
    moreMsg = false;
    recvTime = millis();
    while (millis() < recvTime + window)
    {
        while (port.available() > 0)
        {
            // Bound the read by the REMAINING space, not a fixed 2048. recvData is 2048 bytes
            // and recvLen may already be >0 from a previous chunk, so readBytes(.., 2048) would
            // write past the end and corrupt the members after recvData. The recvLen guard
            // below only fires AFTER the write - too late to prevent the overflow.
            uint16_t len = port.readBytes(recvData + recvLen, sizeof(recvData) - recvLen);
            if (len == 0)
                break; // nothing actually read -> avoid recvData[-1] when len+recvLen==0
            recvTime = millis(); // set receive time first
            char last = recvData[len + recvLen - 1];
            if (!moreMsg)
            {
                if (recvData[0] == '{')
                {
                    if (last == '@' || last == '#') // long json arrived in one receive
                    {
                        recvTime = 0;
                        len--;
                        info_displayln("\nReceive long json data in 1 receiving");
                    }
                    else
                    {
                        moreMsg = true;
                        recvTime += 10000; // wait for additional 10s for the json config data
                        info_displayln("\nLong json data started, please send next one in 10s");
                    }
                }
            }
            else if (last == '@' || last == '#') // finish receiving (end char '@' or '#')
            {
                moreMsg = false;
                len--; // remove the end character
                recvTime = 0;
                info_displayln("\nLong json data finished, process it now");
            }
            else
            {
                recvTime += 10000; // wait for 10s
                info_displayln("\nLong json data continue receiving, please send next one in 10s");
            }
            recvLen += len;
            if (recvLen >= sizeof(recvData)) // buffer full -> flush and bail
            {
                recvLen = 0;
                recvTime = 0;
                info_displayln("The cmd is too long, please send it again");
                while (port.available() > 0)
                    port.readBytes(recvData, sizeof(recvData));
                return false;
            }
        }
    }
    recvData[recvLen] = '\0';
    return true;
}

void ForteSetting::loop()
{
    drainPending(); // web-queued settings (SettingTask owns the EEPROM writes)

    if (Serial.available() > 0)
    {
        info_displayln("data received from Serial port");
        if (!readCommand(Serial, 30))
            return;
    }
    else if (!gBtReleased && SerialBT.available() > 0)
    {
        info_displayln("data received from BT");
        if (!readCommand(SerialBT, 100))
            return;
    }
    else
    {
        return;
    }

    if (recvLen == 1)
    {
        _sensor6035.OptoCommandProcess(recvData[0]);
    }
    else if (recvLen) // if there is no data received in 5ms, then start to process the data
    {
        /* code */
        recvData[recvLen] = 0; // finish the receiving, put 0 to indicate the end of the char array
        info_displayln(recvData);
        // String strCmd = recvData.c_str();

        /// "Buzzer", beep one time for 1 seond
        /// "Buzzer p1_on p2_off p3_times p4_long_off, p5_long_times";
        ///     1. config the buzzer to beep p1_on ms;
        ///     2. stop for p2_off ms
        ///     3. repeat step 1 and 2 for p3_times times
        ///     4. stop p4_long_off ms
        ///     5. repeat step 1 to 4 for p5_long_times times
        if (BuzzerConfig())
        {
        }
        /// "Fan On": turn on the Fan
        /// "Fan...": other will turn off the Fan
        else if (FanConfig()) // Fan on: on, Fanxxx: off
        {
        }
        /// "HeaterSimulate" will stop all heating and simulate the temperature of all heaters by time increase
        else if (HeaterSimuConfig()) // HeaterSimulate value
        {
        }
        /// "TemperatureOutput" will invert temperature output, to start or stop
        else if (TemperatureOutput()) // HeaterSimulate value
        {
        }
        /// "StepSet Amp": Skip lysis and start the Amplification stage
        /// "StepSet Measure": Skip lysis and amplification preheat with heater simulation, start opto reading directly
        else if (HeaterStepSet())
        {
        }

        else if (start_amplification_simulation())
        {
        }
        /// "{...}@"" to analyze the json data with parameter inside, then write into EEPROM
        /// input the right whole json data directly, then it will get all elements and write into EEPROM
        else if (JsonDataConfig())
        {
        }
        /// "ParaRead" will return all parameter get from EEPROM with its key value
        else if (ParaRead())
        {
        }
        /// "EEPROMRead" will read all the EEPROM data and response with 64 hexadecimal number(32 bytes)
        else if (EEPROMRead())
        {
        }
        /// "getResult" will read all old data stored in the EEPROM, then calculate it again to form the output
        else if (resultOutput())
        {
        }
        /// "Res" will restart the device
        else if (restart())
        {
        }
        else
        {
            info_displayln("Command is not supported!");
        }
        recvLen = 0;
        recvTime = 0;
    }
}

/***********************************************************************
 * Function: rerun()
 * Description: Re-runs / restarts the dependent subsystems by forwarding to
 *  _PIDControl.rerun(), _displayCLD.rerun() and _sensor6035.rerun(), used to
 *  restart a measurement cycle. Does not touch configuration storage.
 * pramameter: none
 *  return: none
 */
void ForteSetting::rerun()
{
    _PIDControl.rerun();
    _displayCLD.rerun();
    _sensor6035.rerun();
}

ForteSetting _ForteSetting;
