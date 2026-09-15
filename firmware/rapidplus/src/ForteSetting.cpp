/*
Support parameter configuration by serial communication
To receive the full command, here will wait 10ms after receiving, if there is no further data, then stop to process the data
*/

#include "ForteSetting.h"
#include "Bluetooth.h"
#include "webDashboard.h" // dashboardDeviceBusy(): re-checked before applying web settings
#include "updateOTA.h"    // checkFirmware(): run off AsyncTCP via PEND_OTACHECK
#include "wifiStore.h"    // wifiStoreAdd(): remember each saved WiFi in /wifi.json
#include "Alg/Algo.h"     // LEGACY_MIN_*: the pair removed_by_new_gate() measures against

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
    JsonDocument json_document;
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
        if (!json_document["para version"].isNull())
        {
            info_displayln("parameter configuration");
            if (sizeof(parameter) > 512 - 110)
            {
                info_displayln("Parameter is too long");
                return true;
            }

            // if (!json_document["para version"].isNull())
            {
                String paraVersion = json_document["para version"].as<String>();
                // strlcpy, not strcpy: this also runs from Serial/BT where nothing
                // validated the length, and both fields are char[10] near the top of
                // parastructure - an overrun lands on slopes/origins/kpid, then commits.
                strlcpy(parameter.para_version, paraVersion.c_str(), sizeof(parameter.para_version));
                info_displayln("Para Version: " + paraVersion);
            }

            if (!json_document["PCB version"].isNull())
            {
                String PCBVersion = json_document["PCB version"].as<String>();
                strlcpy(parameter.PCB_version, PCBVersion.c_str(), sizeof(parameter.PCB_version));
                info_displayln("PCB Version: " + PCBVersion);
            }

            // Captured BEFORE any overwrite below: the reboot at the end of this function must
            // fire only if the ID really changed. Serial/BT re-push identical configs routinely.
            String idBefore(parameter.device_id);

            // Print the extracted data
            if (!json_document["opto calibration"].isNull())
            {
                JsonObject calibration = json_document["opto calibration"];
                loadJsonArr(calibration["slopes"].as<JsonArray>(), parameter.slopes, "Calibration - Slopes:");
                loadJsonArr(calibration["origins"].as<JsonArray>(), parameter.origins, "Calibration - Origins:");
            }

            if (!json_document["LED power"].isNull())
                loadJsonArr(json_document["LED power"].as<JsonArray>(), parameter.led_power, "LED Power:");

            // extract the parameter
            if (!json_document["parameters"].isNull())
            {
                JsonObject json_object = json_document["parameters"];
                if (!json_object["min increase"].isNull())
                {
                    double min_inc = json_object["min increase"];
                    parameter.min_increase = min_inc;
                    info_displayln("min increase: " + String(min_inc));
                }

                if (!json_object["min sharpness"].isNull())
                {
                    double min_shp = json_object["min sharpness"];
                    parameter.min_sharpness = min_shp;
                    info_displayln("min sharpness: " + String(min_shp));
                }

                if (!json_object["min slight positive time"].isNull())
                {
                    double min_spt = json_object["min slight positive time"];
                    parameter.min_slight_positive_time = min_spt;
                    info_displayln("min slight positive time: " + String(min_spt));
                }

                if (!json_object["detect shape"].isNull())
                {
                    bool detect_shp = json_object["detect shape"];
                    parameter.detect_shape = detect_shp;
                    info_displayln("detect shape: " + String(detect_shp));
                }

                if (!json_object["detection margin time"].isNull())
                {
                    double value = json_object["detection margin time"];
                    parameter.detection_margin_time = value;
                    info_displayln("detection margin time: " + String(value));
                }

                if (!json_object["arm percentile"].isNull())
                {
                    double value = json_object["arm percentile"];
                    parameter.arm_percentile = value;
                    info_displayln("arm percentile: " + String(value));
                }

                if (!json_object["transition percentile"].isNull())
                {
                    double value = json_object["transition percentile"];
                    parameter.transition_percentile = value;
                    info_displayln("transition percentile: " + String(value));
                }

                if (!json_object["sg order"].isNull())
                {
                    uint8_t value = json_object["sg order"];
                    parameter.sg_order = value;
                    info_displayln("sg order: " + String(value));
                }

                if (!json_object["sg window"].isNull())
                {
                    uint8_t value = json_object["sg window"];
                    parameter.sg_window = value;
                    info_displayln("sg window: " + String(value));
                }

                if (!json_object["baseline start"].isNull())
                {
                    uint8_t value = json_object["baseline start"];
                    parameter.baseline_start = value;
                    info_displayln("baseline start: " + String(value));
                }

                if (!json_object["baseline range"].isNull())
                {
                    uint8_t value = json_object["baseline range"];
                    parameter.baseline_range = value;
                    info_displayln("baseline range: " + String(value));
                }
            }

            if (!json_document["units"].isNull())
            {
                String units = json_document["units"].as<String>();
                strlcpy(parameter.units, units.c_str(), sizeof(parameter.units));
                info_displayln("Units: " + units);
            }

            if (!json_document["device ID"].isNull())
            {
                String deviceId = json_document["device ID"].as<String>();
                strlcpy(parameter.device_id, deviceId.c_str(), sizeof(parameter.device_id));
                info_displayln("Device ID: " + deviceId);
            }

            if (!json_document["lysis duration"].isNull())
            {
                uint16_t lysisDuration = json_document["lysis duration"];
                // parameter.LYSIS_DURATION = lysisDuration;
                parameter.lysisDuration = lysisDuration;
                info_displayln("Lysis duration: " + String(lysisDuration));
            }

            if (!json_document["opto preheat time"].isNull())
            {
                uint16_t optopreheatduraton = json_document["opto preheat time"];
                parameter.optopreheatduration = optopreheatduraton;
                info_displayln("opto preheat time: " + String(optopreheatduraton));
            }

            if (!json_document["LED Duration"].isNull())
            {
                uint LEDDuration = json_document["LED Duration"];
                parameter.LEDDuration = LEDDuration;
                info_displayln("LED Duration: " + String(LEDDuration));
            }

            if (!json_document["time per loop"].isNull())
            {
                ulong loopDuration = json_document["time per loop"];
                parameter.timePerLoop = loopDuration;
                info_displayln("time per loop: " + String(loopDuration));
            }

            if (!json_document["amplification time"].isNull())
            {
                int amplificationTime = json_document["amplification time"];
                parameter.amplification_time = amplificationTime;
                info_displayln("Amplification time: " + String(amplificationTime));
            }

            if (!json_document["lysis temperature"].isNull())
            {
                float lysisTemp = json_document["lysis temperature"];
                parameter.lysisTemp = lysisTemp;
                info_displayln("lysis temperature: " + String(lysisTemp));
            }

            if (!json_document["amplification temperature"].isNull())
            {
                float ampTemp = json_document["amplification temperature"];
                parameter.amplifTemp = ampTemp;
                info_displayln("amplification temperature: " + String(ampTemp));
            }

            if (!json_document["bottom temperature sensor seq"].isNull())
                loadJsonArr(json_document["bottom temperature sensor seq"].as<JsonArray>(), parameter.bottomTemperatureSensorSq, "bottom temperature sensor seq:");

            if (!json_document["top temperature sensor seq"].isNull())
                loadJsonArr(json_document["top temperature sensor seq"].as<JsonArray>(), parameter.topTemperatureSensorSq, "top temperature sensor seq:");

            if (!json_document["PID parameter"].isNull())
                loadJsonArr(json_document["PID parameter"].as<JsonArray>(), parameter.kpid, "PID parameter of bottom heater1:");

            if (!json_document["PID2 parameter"].isNull())
                loadJsonArr(json_document["PID2 parameter"].as<JsonArray>(), parameter.kpid2, "PID2 parameter of bottom heater2&3:");

            if (!json_document["PID3 parameter"].isNull())
                loadJsonArr(json_document["PID3 parameter"].as<JsonArray>(), parameter.kpid3, "PID3 parameter of top hotlid2&3:");

            if (!json_document["Bottom overheat value"].isNull())
                loadJsonArr(json_document["Bottom overheat value"].as<JsonArray>(), parameter.bottomOverheat, "Bottom overheat value:");

            if (!json_document["Top overheat value"].isNull())
                loadJsonArr(json_document["Top overheat value"].as<JsonArray>(), parameter.topOverheat, "Top overheat value:");

            if (!json_document["temperature value calibration"].isNull())
                loadJsonArr(json_document["temperature value calibration"].as<JsonArray>(), parameter.temperatureOffset, "temperature value calibration");

            if (!json_document["top heater PWM"].isNull())
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

            if (!json_document["buzzer"].isNull())
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

            if (!json_document["kitId"].isNull())
            {
                double kitId = json_document["kitId"];
                parameter.kitId = kitId;
                info_displayln("kitId: " + String(kitId));
            }

            if (!json_document["empty"].isNull())
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

            if (!json_document["counter"].isNull()) // for test purpose only, to show the diagram better, it won't be saved in the EEPROM
            {
                _sensor6035.setCounterDisplayflag(true);
            }
            else
            {
                _sensor6035.setCounterDisplayflag(false);
            }

            parameter.length = sizeof(parameter); // use this to indicate the EEPROM has valid parameter
            eepromLock();
            EEPROM.begin(_EEPROM_SIZE);
            EEPROM.put(PARAMETERPOS, parameter);
            EEPROM.commit();
            EEPROM.end();
            eepromUnlock();

            // Serial/BT reach this parser with NOTHING validating them (CLAUDE.md Setting #4) -
            // the web is the only caller that goes through handleConfigPost. strlcpy above bounds
            // the LENGTH, but a control character or an empty string still gets through, and this
            // one field now feeds the SoftAP SSID, the mDNS label and the QR payload. Re-run the
            // same check begin() applies, so every way into the store lands on a usable value.
            if (!json_document["device ID"].isNull())
            {
                sanitiseDeviceId();
                // Same reason as PEND_ID: the ID is latched into the radio at boot (softAP SSID,
                // DHCP hostname, mDNS) and cannot be changed in place. THIS path matters more,
                // not less: unlike POST /deviceid it has no busy gate, so Serial/BT can rename
                // mid-run - and dashboardRequestRestart() is what holds the reboot until the run
                // is over instead of cutting it.
                if (idBefore != parameter.device_id)
                {
                    info_displayln("[cfg] device id changed - reboot queued to re-announce it");
                    dashboardRequestRestart(1500);
                }
            }
            return true;
        }
        else if (!json_document["raw_data"].isNull()) // include raw data which means for the testing purpose
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
    eepromLock();
    EEPROM.begin(_EEPROM_SIZE);
    Word tmp[10 * 130] = {0};
    EEPROM.get(RECORDPOS, tmp);

    memcpy(_sensor6035.sensor67Value, tmp, sizeof(tmp));

    EEPROM.end();
    eepromUnlock();
    _displayCLD.changeScreen = true;
    _displayCLD.type_infor = escreenReview;
    return true;
}

/// "uploadResult" will re-send the stored record to the cloud (GAS + ingest + ERP)
/// @return true if right command, or it return false
/***********************************************************************
 * Function: uploadResult()
 * Description: Parses a serial "uploadResult" command from recvData and puts
 *  the display on the manual-upload screen (eUpLoadData), which is exactly
 *  what RED on the on-device Setting menu does: screen_Result('f') re-reads the
 *  record at RECORDPOS, re-analyses it and posts it through
 *  postData_GoogleSheet() with type_Upload "Manual". Nothing else in the
 *  firmware could start an upload from the UART - "getResult" deliberately
 *  reviews without posting (escreenReview), so a bench dataset injected with
 *  {"Slot":[..]}n# could be graded but never reach the server.
 *  Refused (and says so) while the device is busy - the upload parks
 *  DisplayTask in mbedTLS for up to ~90 s and suspends the dashboard, so it
 *  must never start on top of a run; eUpLoadData itself counts as busy, so a
 *  second command cannot stack a second upload on one still in flight - and
 *  when there is no STA link, where screen_Result('f') would silently skip
 *  the post and only redraw the grid.
 * pramameter: none (reads member recvData)
 *  return: true if recvData starts with "uploadResult", false otherwise
 */
bool ForteSetting::uploadResult()
{
    if (strncasecmp(recvData, "uploadResult", 12))
    {
        return false;
    }
    if (dashboardDeviceBusy())
    {
        info_displayln("[up] uploadResult refused: device busy");
        return true;
    }
    if (dashboardIsAP() || WiFi.status() != WL_CONNECTED)
    {
        info_displayln("[up] uploadResult refused: no STA link, screen_Result('f') would skip the post");
        return true;
    }
    // State first, redraw flag second: DisplayTask polls changeScreen on another core, and
    // seeing the flag before the new state would redraw the OLD screen and clear the flag.
    _displayCLD.type_infor = eUpLoadData;
    _displayCLD.changeScreen = true;
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
    JsonDocument json_document;
    DeserializationError error = deserializeJson(json_document, recvData);
    if (error)
    {
        info_display("Error parsing JSON: ");
        info_displayln(error.c_str());
        return false;
    }
    else
    {
        if (!json_document["Slot"].isNull())
        {
            for (size_t i = 0; i < loops; i++)
            {
                _sensor6035.sensor67Value[slot][i] = json_document["Slot"][i];
            }
        }
    }
    eepromLock();
    EEPROM.begin(_EEPROM_SIZE);
    Word tmp[10 * 130] = {0};
    memcpy(tmp, _sensor6035.sensor67Value, sizeof(tmp));
    EEPROM.put(RECORDPOS, tmp);
    delay(100);
    EEPROM.commit();
    EEPROM.end();
    eepromUnlock();

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

// ADDR_CHECK_ID_DEVICE stamped with this = "the slot-170 migration below already ran". The byte
// carries NO identity; v2.4.2 used the same address as a bool flag. Read it with EEPROM.read(),
// never readBool(): a virgin 0xFF reads as TRUE and would skip the migration on every fresh unit.
static const uint8_t kIdMigrated = 0xA5;

// What v2.4.3a expects to find in a SAVED config, in the units an operator speaks. These are the
// yardstick for configSelfCheckJson(); the migration below writes the struct fields they describe.
// Kept as one list so the check and the migration cannot drift apart into disagreeing about what
// "correct" is.
static const double kExpectLysisMin = 10.0;      // parameter.lysisDuration 600 s
static const double kExpectCallingMin = 30.0;    // amplification_time 90 x timePerLoop 20 s
static const double kExpectMinIncrease = 25.0;   //
static const double kExpectMinSharpness = 8.0;    // see define.h for why not 11.0
static const double kExpectDetectMargin = 4.0;   // == baseline_start + baseline_range
static const double kTimingTolMin = 0.02;        // ~1 s: these are exact values, not measurements

// ADDR_CONFIG_REV as begin() last saw it, cached in RAM because the self-check is served from
// AsyncTCP and that task must never open EEPROM (CLAUDE.md Setting #2). 0 = never stamped.
static uint8_t gConfigRevSeen = 0;

// Strings no operator ever typed. Two are compiled defaults (this build's, and v2.4.2's
// "proto 0"); the third is the one that makes this list load-bearing:
//
//   v2.4.2:src/Bluetooth.cpp - WiFiManagerParameter custom_id_device(..., "RPL", 40)
//
// the portal PRE-FILLED its ID box with "RPL" and saved whatever was in it, so every operator who
// opened the portal just to enter WiFi wrote "RPL" into slot 170. Adopting that as a real serial
// would give a large slice of the fleet ONE shared identity - all uploading under it, all
// answering at one .local name - and it would pass sanitiseDeviceId(), permanently hiding the
// UNSET prompt. v2.4.2 itself treated it as "unconfigured" (strncmp(id,"RPL",3) picked the AP name).
//
// EXACT compare, never a prefix: real serials look like "RPL03010" and must survive.
static bool idIsPlaceholder(const char *id)
{
    static const char *const kNeverTyped[] = {"RPL", "proto 0", "UNSET"};
    for (const char *p : kNeverTyped)
        if (strcmp(id, p) == 0)
            return true;
    return false;
}

// parameter.device_id is THE device ID - the only store since the id_device global went away
// (2026-07-30). Everything reads it through the protoID macro: the web header, the SoftAP SSID,
// the mDNS label, the QR payload and every Google Sheet / ERP upload. So it gets sanitised in
// exactly one place - here, right after the struct is loaded - and no consumer has to.
//
// Two things can be wrong with it:
//  - NOT NUL-TERMINATED. begin() fills the struct with a raw EEPROM.get(); a block written by an
//    older layout can leave all 10 bytes non-zero, and then every strlen/String read walks off
//    the end of the field into slopes[].
//  - NOISE. Every writer produces 1..9 printable characters (POST /deviceid validates 1..9,
//    handleConfigPost rejects > 9, the field is char[10]), so anything else is provably not a
//    real write. Passing noise on is worse than admitting the ID is unset: an over-long value
//    stops WiFi.softAP() (802.11 caps an SSID at 32 B) and used to reboot the machine through
//    the QR encoder (docs/history/2026-07-29-qr-reset-ssid-drift.md).
//
// "UNSET" shows on the TFT start screen AND the web header, so the operator sees the machine
// needs configuring instead of trusting a plausible-looking accident. Not persisted: the next
// real save is what writes it, and until then the prompt should keep coming back.
void ForteSetting::sanitiseDeviceId()
{
    parameter.device_id[sizeof(parameter.device_id) - 1] = '\0'; // terminate BEFORE reading it
    // A compiled default / pre-filled portal value is "no ID", not an ID - see idIsPlaceholder().
    // Checked HERE so every write path inherits it: JsonDataConfig() (Serial/BT + POST /config)
    // and drainPending() PEND_ID (POST /deviceid) both call this before they persist.
    bool ok = parameter.device_id[0] != '\0' && !idIsPlaceholder(parameter.device_id);
    for (size_t i = 0; ok && parameter.device_id[i]; i++)
        if (parameter.device_id[i] < 0x20 || parameter.device_id[i] > 0x7E)
            ok = false;
    if (!ok)
    {
        Serial.println("[id] no usable device ID in EEPROM -> \"UNSET\"");
        strlcpy(parameter.device_id, "UNSET", sizeof(parameter.device_id));
    }
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
        //
        // Gated on the DATA, never on FirmwareVer: that global changes every release, so a unit
        // jumping 2.4.2 -> 2.4.4 would skip this and run the hotlid PID with kpid3 == {0,0,0}.
        if (parameter.kpid3[0] == 0 && parameter.kpid3[1] == 0 && parameter.kpid3[2] == 0)
        {
            parameter.kpid3[0] = 60;
            parameter.kpid3[1] = 0.1;
            parameter.kpid3[2] = 40;
            parameter.length = sizeof(parameter);
            EEPROM.put(PARAMETERPOS, parameter);
            EEPROM.commit();
            info_displayln("kpid3 seeded to defaults and saved");
        }

        // ---- one-time migration: v2.4.3a timings and analysis thresholds ---------------------
        // See ADDR_CONFIG_REV (define.h) for why editing a compiled default is not enough. This
        // is a one-shot STAMP, not a value compare, so an operator who deliberately retunes a
        // value afterwards is not overruled on the next boot - the rule the slot-170 ID migration
        // follows. A value compare is what kpid3 above can afford: {0,0,0} is impossible for a
        // real PID, whereas 120 rounds is a perfectly legitimate setting someone may have chosen.
        uint8_t cfgRev = EEPROM.read(ADDR_CONFIG_REV);
        if (cfgRev == 0xFF)
            cfgRev = 0; // virgin byte reads 255 - that is "never stamped", NOT revision 255
        if (cfgRev < CONFIG_REV_THRESHOLDS)
        {
            // CALIBRATION IS NOT OURS TO TOUCH. slopes and origins are measured per machine
            // (slopes run 0.50-3.50 across the fleet) and led_power is set per slot at
            // commissioning. Resetting either to the compiled placeholder is worse than not
            // migrating at all: the machine keeps working and every reported number is wrong.
            // Copied out before the edit, compared after, and any difference abandons the whole
            // write - a half-correct config saved to EEPROM is the one state with no easy way back.
            float keepSlopes[10];
            float keepOrigins[10];
            uint8_t keepLed[10];
            memcpy(keepSlopes, parameter.slopes, sizeof(keepSlopes));
            memcpy(keepOrigins, parameter.origins, sizeof(keepOrigins));
            memcpy(keepLed, parameter.led_power, sizeof(keepLed));

            parameter.lysisDuration = 600;         // 10 min
            parameter.amplification_time = 90;     // 90 rounds x 20 s = 30 min (was 120 = 40 min)
            parameter.min_increase = 25.0;         //
            parameter.min_sharpness = 8.0;         // the gate removed_by_new_gate() needs to differ
            parameter.detection_margin_time = 4.0; //   from LEGACY_MIN_* or it can never fire
            parameter.baseline_start = 2;          // baseline_start + baseline_range
            parameter.baseline_range = 2;          //   MUST equal detection_margin_time
            parameter.arm_percentile = 0.5;

            const bool calibIntact =
                memcmp(keepSlopes, parameter.slopes, sizeof(keepSlopes)) == 0 &&
                memcmp(keepOrigins, parameter.origins, sizeof(keepOrigins)) == 0 &&
                memcmp(keepLed, parameter.led_power, sizeof(keepLed)) == 0;

            if (calibIntact)
            {
                parameter.length = sizeof(parameter);
                EEPROM.put(PARAMETERPOS, parameter);
                EEPROM.write(ADDR_CONFIG_REV, CONFIG_REV_THRESHOLDS);
                EEPROM.commit();
                info_displayf("[cfg] migrated to rev %d: lysis 10 min, calling 30 min,"
                              " min_increase 25.0, min_sharpness 8.0"
                              " - slopes/origins/LED power untouched\n",
                              CONFIG_REV_THRESHOLDS);
            }
            else
            {
                // Unreachable through the assignments above. It becomes reachable the moment
                // someone adds a field to this block, which is exactly when it needs to fire.
                memcpy(parameter.slopes, keepSlopes, sizeof(keepSlopes));
                memcpy(parameter.origins, keepOrigins, sizeof(keepOrigins));
                memcpy(parameter.led_power, keepLed, sizeof(keepLed));
                info_displayln("[cfg] MIGRATION ABANDONED - it would have altered calibration."
                               " Nothing written; the machine keeps its saved config.");
            }
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
    sanitiseDeviceId(); // the ONE place the ID store is populated - see the function comment

    // ---- one-time migration: the pre-2.4.3 device ID lived at EEPROM slot 170 ----------------
    // v2.4.2 kept the OPERATIONAL id there (a 40-byte Arduino String written by the WiFiManager
    // portal) and THAT is the value it sent as "id_device" to the Google Sheet, the ERP and the
    // error uploads; parameter.device_id was a rarely-written annotation defaulting to "proto 0".
    // So on the first 2.4.3 boot slot 170 WINS - it is the identity the cloud already knows.
    // Afterwards parameter.device_id is the only store (CLAUDE.md Setting #6) and nothing here
    // may ever revert an ID the operator sets later - hence a one-shot STAMP, not a value compare
    // (comparing values would fight every later rename, forever).
    //
    // Slot 170 is never written or cleared: a downgrade to v2.4.2 must still find its ID there.
    // Gated on the DATA, never on FirmwareVer - a unit jumping 2.4.2 -> 2.4.4 must migrate too.
    if (EEPROM.read(ADDR_CHECK_ID_DEVICE) != kIdMigrated)
    {
        // BOUNDED read. EEPROM.readString() scans for a NUL to the end of the 4096-byte buffer,
        // NOT to this slot's 40-byte boundary (170..209) - that is how a neighbouring field's
        // bytes turn into a several-hundred-character "ID".
        char legacy[41];
        for (int i = 0; i < 40; i++)
            legacy[i] = (char)EEPROM.read(ADDR_ID_DEVICE_BASE + i);
        legacy[40] = '\0';

        size_t n = strlen(legacy);
        bool usable = n >= 1 && n <= sizeof(parameter.device_id) - 1 && !idIsPlaceholder(legacy);
        // (unsigned char): char is signed on xtensa, so a virgin 0xFF byte would compare < 0x20.
        for (size_t i = 0; usable && i < n; i++)
            if ((unsigned char)legacy[i] < 0x20 || (unsigned char)legacy[i] > 0x7E)
                usable = false;

        if (usable)
        {
            // strcmp is safe on parameter.device_id: sanitiseDeviceId() ran above, so it is
            // terminated, and legacy is <= 9 chars so the compare stops inside the char[10].
            if (strcmp(legacy, parameter.device_id) != 0)
            {
                strlcpy(parameter.device_id, legacy, sizeof(parameter.device_id));
                Serial.printf("[id] migrated device ID from EEPROM slot %d -> '%s'\n",
                              ADDR_ID_DEVICE_BASE, parameter.device_id);
            }
        }
        else if (n)
        {
            // Covers 10..40-char legacy IDs: the portal field accepted 40 and nothing clamped it.
            // Truncating would upload an ID matching no record - worse than admitting none. The
            // machine keeps sanitiseDeviceId()'s "UNSET" and asks for it, loudly.
            Serial.printf("[id] legacy slot %d holds %u bytes, not a usable device ID"
                          " - re-enter it in Setting\n",
                          ADDR_ID_DEVICE_BASE, (unsigned)n);
        }

        // Persist + stamp ONLY with a valid parameter block. On a fresh/erased EEPROM the else
        // branch above deliberately writes nothing, so the "please initialize" prompt keeps
        // coming and the stamp stays unwritten - the migration retries until it can stick.
        if (paraEEPROM.length == sizeof(parameter))
        {
            parameter.length = sizeof(parameter);
            EEPROM.put(PARAMETERPOS, parameter);
            EEPROM.write(ADDR_CHECK_ID_DEVICE, kIdMigrated);
            EEPROM.commit();
        }
    }
    // OpticalUnits = parameter.units;//"counts";
    gConfigRevSeen = EEPROM.read(ADDR_CONFIG_REV);
    if (gConfigRevSeen == 0xFF)
        gConfigRevSeen = 0;
    EEPROM.end();

    configSelfCheckLog();
}

/***********************************************************************
 * Function: configSelfCheckJson()
 * Description: Reports what this machine is ACTUALLY set to run - lysis time and
 *  calling (amplification) time in minutes, the two analysis gates, and the
 *  baseline window - each beside the value v2.4.3a expects, plus one overall
 *  pass/fail. Everything is read from the live parameter struct, so it describes
 *  the machine and not the source; reading the compiled defaults is precisely the
 *  mistake this exists to catch, because begin() has already replaced them with
 *  the EEPROM copy by the time anything runs.
 *
 *  Timings come out in MINUTES because that is the unit the operator and the run
 *  sheet speak. The struct stores seconds (lysis) and rounds (calling), and the
 *  conversion for calling uses the machine's OWN timePerLoop rather than a
 *  hardcoded 20 s - a unit set to a different round length would otherwise be
 *  told it is fine when it is running a different protocol entirely.
 *
 *  CALIBRATION IS REPORTED, NOT GRADED. slopes, origins and led_power are
 *  per-machine; there is no correct fleet-wide value to check them against. What
 *  can be said is whether they are still the compiled placeholders (every slope
 *  1.0), which is what a wiped or never-calibrated unit looks like - and that is
 *  the state this migration had to be written carefully to avoid producing.
 * pramameter: none
 *  return: String - JSON, the same document GET /selfcheck serves
 */
String ForteSetting::configSelfCheckJson()
{
    JsonDocument doc;

    const double lysisMin = parameter.lysisDuration / 60.0;
    const double callingMin =
        parameter.amplification_time * (double)parameter.timePerLoop / 60000.0;
    const double baselineEnd = (double)parameter.baseline_start + (double)parameter.baseline_range;

    const bool okRev = gConfigRevSeen >= CONFIG_REV_THRESHOLDS;
    const bool okLysis = fabs(lysisMin - kExpectLysisMin) < kTimingTolMin;
    const bool okCalling = fabs(callingMin - kExpectCallingMin) < kTimingTolMin;
    const bool okIncrease = fabs(parameter.min_increase - kExpectMinIncrease) < 0.001;
    const bool okSharpness = fabs(parameter.min_sharpness - kExpectMinSharpness) < 0.001;
    const bool okMargin = fabs(parameter.detection_margin_time - kExpectDetectMargin) < 0.001;
    // The baseline window must CLOSE where the earliest legitimate Ct opens, or an early
    // amplifier has its zero measured against its own rise (define.h, baseline_start).
    const bool okBaseline = fabs(baselineEnd - parameter.detection_margin_time) < 0.001;
    // The one check that is about behaviour rather than values: removed_by_new_gate() compares
    // the runtime pair against LEGACY_MIN_*, so if the runtime pair still EQUALS the legacy pair
    // the review gate is inert and no well can ever be called F or flipped to N. A machine can
    // pass every other line here and still have the feature silently switched off.
    const bool okGateLive = (parameter.min_increase > LEGACY_MIN_INCREASE) ||
                            (parameter.min_sharpness > LEGACY_MIN_SHARPNESS);

    bool slopesDefault = true;
    for (uint8_t i = 0; i < 10; i++)
        if (fabs(parameter.slopes[i] - 1.0f) > 0.0001f)
            slopesDefault = false;

    doc["firmware"] = FirmwareVer;
    doc["config rev"] = gConfigRevSeen;
    doc["expected rev"] = CONFIG_REV_THRESHOLDS;

    JsonObject t = doc["timings"].to<JsonObject>();
    t["lysis min"] = lysisMin;
    t["lysis expected min"] = kExpectLysisMin;
    t["lysis ok"] = okLysis;
    t["calling min"] = callingMin;
    t["calling expected min"] = kExpectCallingMin;
    t["calling ok"] = okCalling;
    t["rounds"] = parameter.amplification_time;
    t["ms per round"] = (uint32_t)parameter.timePerLoop;

    JsonObject g = doc["gates"].to<JsonObject>();
    g["min increase"] = parameter.min_increase;
    g["min increase expected"] = kExpectMinIncrease;
    g["min increase ok"] = okIncrease;
    g["min sharpness"] = parameter.min_sharpness;
    g["min sharpness expected"] = kExpectMinSharpness;
    g["min sharpness ok"] = okSharpness;
    g["detection margin time"] = parameter.detection_margin_time;
    g["detection margin ok"] = okMargin;
    g["baseline window closes"] = baselineEnd;
    g["baseline window ok"] = okBaseline;
    g["review gate live"] = okGateLive;

    // Reported so the operator can compare against the calibration sheet before and after a
    // flash. This is the data the migration promises not to touch.
    JsonObject c = doc["calibration"].to<JsonObject>();
    c["still compiled defaults"] = slopesDefault;
    JsonArray sl = c["slopes"].to<JsonArray>();
    JsonArray og = c["origins"].to<JsonArray>();
    JsonArray lp = c["led power"].to<JsonArray>();
    for (uint8_t i = 0; i < 10; i++)
    {
        sl.add(parameter.slopes[i]);
        og.add(parameter.origins[i]);
        lp.add(parameter.led_power[i]);
    }

    // slopesDefault is NOT part of pass/fail: a genuinely uncalibrated new machine is not a
    // failed migration, and folding it in here would make this check cry wolf on the bench.
    doc["pass"] = okRev && okLysis && okCalling && okIncrease && okSharpness && okMargin &&
                  okBaseline && okGateLive;

    String out;
    serializeJson(doc, out);
    return out;
}

/***********************************************************************
 * Function: configSelfCheckLog()
 * Description: Prints the self-check as four readable lines at the end of begin(),
 *  so a unit being commissioned over USB shows whether the migration landed without
 *  anyone needing to reach the web route. Deliberately louder on failure than on
 *  success: a PASS is one line, a FAIL names what is wrong.
 * pramameter: none
 *  return: none
 */
void ForteSetting::configSelfCheckLog()
{
    const double lysisMin = parameter.lysisDuration / 60.0;
    const double callingMin =
        parameter.amplification_time * (double)parameter.timePerLoop / 60000.0;
    const bool gateLive = (parameter.min_increase > LEGACY_MIN_INCREASE) ||
                          (parameter.min_sharpness > LEGACY_MIN_SHARPNESS);

    info_displayf("[chk] lysis %.1f min (want %.1f) | calling %.1f min (want %.1f)"
                  " | %d rounds x %lu ms\n",
                  lysisMin, kExpectLysisMin, callingMin, kExpectCallingMin,
                  (int)parameter.amplification_time, (unsigned long)parameter.timePerLoop);
    info_displayf("[chk] min_increase %.1f (want %.1f) | min_sharpness %.1f (want %.1f)"
                  " | review gate %s\n",
                  parameter.min_increase, kExpectMinIncrease,
                  parameter.min_sharpness, kExpectMinSharpness,
                  gateLive ? "LIVE" : "INERT");
    info_displayf("[chk] baseline [%d,%d) closes at %d, detection margin %.1f\n",
                  (int)parameter.baseline_start,
                  (int)(parameter.baseline_start + parameter.baseline_range),
                  (int)(parameter.baseline_start + parameter.baseline_range),
                  parameter.detection_margin_time);

    // Braces are load-bearing: info_display* expands to an unbraced `if (!gBtReleased) ...`
    // (define.h), so an unbraced arm here swallows the else.
    String js = configSelfCheckJson();
    if (js.indexOf("\"pass\":true") >= 0)
    {
        info_displayf("[chk] PASS - config rev %d\n", (int)gConfigRevSeen);
    }
    else
    {
        info_displayf("[chk] FAIL - config rev %d, expected %d."
                      " Machine is NOT running the v2.4.3a settings: %s\n",
                      (int)gConfigRevSeen, CONFIG_REV_THRESHOLDS, js.c_str());
    }
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
/// "uploadResult" will re-send the stored record to the cloud (eUpLoadData, type_Upload "Manual")
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

bool ForteSetting::postOtaCheck(bool promptOnDevice)
{
    if (pendingKind != PEND_NONE)
        return false;
    otaPromptOnDevice = promptOnDevice; // payload, so it must be written BEFORE the flag
    __sync_synchronize();
    pendingKind = PEND_OTACHECK;
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
        // Do NOT commit here - a wrong password would overwrite the working network and
        // strand the machine. Park the new credentials as a TRIAL and reboot; setup()
        // tests them and only commits (EEPROM preferred + list front) if they actually
        // connect, otherwise it reverts to the previous network and reports the failure.
        // (Verifying a password needs a real association, which needs a reboot - runtime
        // WiFi.begin deadlocks async_tcp.)
        wifiStoreSetTrial(pendingA, pendingB);
        info_displayln("[cfg] WiFi trial queued: " + pendingA);
        restartAt = millis() + 1500;
    }
    else if (kind == PEND_ID)
    {
        String before(parameter.device_id);
        strlcpy(parameter.device_id, pendingA.c_str(), sizeof(parameter.device_id));
        // POST /deviceid bounds the LENGTH (1..9) but not the BYTES, and this field feeds the
        // SoftAP SSID, the mDNS label and the QR payload. Run the same trust boundary begin()
        // uses, BEFORE the write, so a hostile POST persists "UNSET" rather than control bytes.
        sanitiseDeviceId();
        parameter.length = sizeof(parameter);
        eepromLock();
        EEPROM.begin(_EEPROM_SIZE);
        EEPROM.put(PARAMETERPOS, parameter);
        EEPROM.commit();
        EEPROM.end();
        eepromUnlock();
        info_displayln("[cfg] device id: " + String(parameter.device_id));

        // The ID is LATCHED into the radio at boot in three places - WiFi.softAP() (the SoftAP
        // SSID), WiFi.setHostname() (the DHCP name) and MDNS.begin() (<id>.local) - and this
        // core has no API to change any of them in place. Writing EEPROM alone left the machine
        // announcing the OLD name while the QR screen and the web reported the NEW one: the
        // operator could not rejoin the hotspot until a manual power cycle.
        //
        // Only when the value actually CHANGED: the Setting card pre-fills the current ID, so
        // pressing Save without editing must not reboot the instrument.
        //
        // dashboardRequestRestart(), not ESP.restart(): it defers until
        // !dashboardDeviceBusy() && !suspended && type_infor != escreenFinished, so a run in
        // progress is never cut. Until it lands, screen_QR() and buildHomeJson() report
        // WiFi.softAPSSID() - the name really on the air - so the machine stays joinable.
        if (before != parameter.device_id)
        {
            info_displayln("[cfg] device id changed - reboot queued to re-announce it");
            dashboardRequestRestart(1500); // matches the PEND_WIFI delay; lets the 200 render
        }
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
            _sensor6035.bResultGet(ct, res); // recompute CT / P-N-S
            dashboardSetResults(ct, res);    // cache for GET /slots
            // /curve length - see sensor6035::scanRunLength(). Shared with screen_Result so
            // the two paths that publish results can never disagree about the run length.
            uint8_t len = _sensor6035.scanRunLength();
            _sensor6035.setLastRunLoops(len);
            info_displayf("[review] reloaded last run from EEPROM (%u rounds)\n", len);
        }
        else
        {
            // No plausible stored run -> leave the cache empty (Result tab shows nothing).
            // Log probe (used to be silent) so a fail is observable: 0xFFFF=virgin flash,
            // 0=erased/EEPROM.begin alloc-failed, small-nonzero/garbage=a torn read from an
            // EEPROM 4KB-buffer double-free (a concurrent error-save on ControlTask).
            info_displayf("[review] no stored run: probe=%u\n", probe);
        }
    }
    else if (kind == PEND_OTACHECK)
    {
        // Blocking HTTPS GET against the ingest server - must not run on AsyncTCP, hence
        // the queue. Two callers, and they disagree about the TFT: the web button passes
        // false (a remote click must not hijack the screen of whoever is standing at the
        // machine), the 6 h poll passes true (nobody is watching a browser, and the prompt
        // is where RED means "install"). See postOtaCheck(). otaState / fwVer carry the
        // answer back to GET /ota either way.
        //
        // Safe to prompt from here: drainPending() only runs while the machine is idle, so
        // the takeover cannot land on a run in progress.
        //
        // Re-checked HERE too, not only where the poll queued it: drainPending() runs up to
        // ~10 ms later and its own busy guard treats escreenFinished as idle. Taking the TFT
        // in that window drops the operator's results screen, and because eUpdateOTA is itself
        // on the idle allowlist the write unlocks the deferred-reboot gate in the middle of
        // the end-of-run pipeline. Closing the TOCTOU costs one condition; the poll just
        // retries at its next deadline.
        if (_displayCLD.type_infor == escreenFinished)
        {
            info_displayln("[ota] check skipped: end-of-run pipeline is running");
        }
        else
        {
            info_displayf("[ota] check (prompt=%d)\n", (int)otaPromptOnDevice);
            checkFirmware(otaPromptOnDevice);
        }
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
                break;           // nothing actually read -> avoid recvData[-1] when len+recvLen==0
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
        /// "uploadResult" will re-send the stored record to the cloud, same path as the "Up Data" menu item
        else if (uploadResult())
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
