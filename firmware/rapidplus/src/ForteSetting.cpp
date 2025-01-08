/*
Support parameter configuration by serial communication
To receive the full command, here will wait 10ms after receiving, if there is no further data, then stop to process the data
*/

#include "ForteSetting.h"
#include "Bluetooth.h"

/// @brief Buzzer control
/// "Buzzer", beep one time for 1 seond
/// "Buzzer p1_on p2_off p3_times p4_long_off, p5_long_times";
///     1. config the buzzer to beep p1_on ms;
///     2. stop for p2_off ms
///     3. repeat step 1 and 2 for p3_times times
///     4. stop p4_long_off ms
///     5. repeat step 1 to 4 for p5_long_times times
/// @return true if command is Buzzerxxx, or false
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
        _sensor6035.setStepeSensorpreheat();
        _displayCLD.type_infor = epreheating67; // updated to start preheat directly in 26 Feb, 2024
        _displayCLD.bheadershow = true;
        _displayCLD.changeScreen = true;

        return true;
    }
    if (!strncasecmp(recvData + 8, "Measure", 7))
    {
        info_displayln("Skip to start the Amplification measurement directly");
        // pidStep = epid23ready;
        _PIDControl.heatSimulation(0xFF);
        _PIDControl.setPID23Ready();
        _displayCLD.type_infor = ewaitampTube;
        _displayCLD.bheadershow = true;
        _displayCLD.changeScreen = true;

        return true;
    }
    return true;
}

/// @brief to analyze the json data with parameter inside, then write into EEPROM
/// input the right whole json data directly, then it will get all elements and write into EEPROM
/// @return return true and all parameter data if it's correct, or return false
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
                JsonArray slopes = calibration["slopes"];
                JsonArray origins = calibration["origins"];

                info_displayln("Calibration - Slopes:");
                for (int i = 0; i < slopes.size(); i++)
                {
                    float f = float(slopes[i]);
                    parameter.slopes[i] = f;
                    info_displayln(f);
                }

                info_displayln("Calibration - Origins:");
                for (int i = 0; i < origins.size(); i++)
                {
                    float f = float(origins[i]);
                    parameter.origins[i] = f;
                    info_displayln(f);
                }
            }

            if (json_document.containsKey("LED power"))
            {
                JsonArray ledPower = json_document["LED power"];
                info_displayln("LED Power:");
                for (int i = 0; i < ledPower.size(); i++)
                {
                    uint8_t u8 = uint8_t(ledPower[i]);
                    parameter.led_power[i] = u8;
                    info_displayln(u8);
                }
            }

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
            {
                JsonArray bottomSensorSeq = json_document["bottom temperature sensor seq"];
                info_displayln("bottom temperature sensor seq:");
                for (uint8_t i = 0; i < bottomSensorSeq.size(); i++)
                {
                    uint8_t u8 = uint8_t(bottomSensorSeq[i]);
                    parameter.bottomTemperatureSensorSq[i] = u8;
                    info_displayln(u8);
                }
            }

            if (json_document.containsKey("top temperature sensor seq"))
            {
                JsonArray topSensorSeq = json_document["top temperature sensor seq"];
                info_displayln("top temperature sensor seq:");
                for (uint8_t i = 0; i < topSensorSeq.size(); i++)
                {
                    uint8_t u8 = uint8_t(topSensorSeq[i]);
                    parameter.topTemperatureSensorSq[i] = u8;
                    info_displayln(u8);
                }
            }

            if (json_document.containsKey("PID parameter"))
            {
                JsonArray pidPara = json_document["PID parameter"];
                info_displayln("PID parameter of bottom heater1:");
                for (uint8_t i = 0; i < pidPara.size(); i++)
                {
                    double tmp = double(pidPara[i]);
                    parameter.kpid[i] = tmp;
                    info_displayln(parameter.kpid[i]);
                }
            }

            if (json_document.containsKey("PID2 parameter"))
            {
                JsonArray pidPara = json_document["PID2 parameter"];
                info_displayln("PID2 parameter of bottom heater2&3:");
                for (uint8_t i = 0; i < pidPara.size(); i++)
                {
                    double tmp = double(pidPara[i]);
                    parameter.kpid2[i] = tmp;
                    info_displayln(parameter.kpid2[i]);
                }
            }

            if (json_document.containsKey("Bottom overheat value"))
            {
                JsonArray overHeat = json_document["Bottom overheat value"];
                info_displayln("Bottom overheat value:");
                for (uint8_t i = 0; i < overHeat.size(); i++)
                {
                    double tmp = double(overHeat[i]);
                    parameter.bottomOverheat[i] = tmp;
                    info_displayln(parameter.bottomOverheat[i]);
                }
            }

            if (json_document.containsKey("Top overheat value"))
            {
                JsonArray overHeat = json_document["Top overheat value"];
                info_displayln("Top overheat value:");
                for (uint8_t i = 0; i < overHeat.size(); i++)
                {
                    double tmp = double(overHeat[i]);
                    parameter.topOverheat[i] = tmp;
                    info_displayln(parameter.topOverheat[i]);
                }
            }

            if (json_document.containsKey("temperature value calibration"))
            {
                JsonArray sensorOffset = json_document["temperature value calibration"];
                info_displayln("temperature value calibration");
                for (uint8_t i = 0; i < sensorOffset.size(); i++)
                {
                    float f = float(sensorOffset[i]);
                    parameter.temperatureOffset[i] = f;
                    info_displayln(f);
                }
            }

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
bool ForteSetting::resultOutput()
{
    if (strncasecmp(recvData, "getResult", 12))
    {
        return false;
    }
    EEPROM.begin(_EEPROM_SIZE);
    Word tmp[10 * 100] = {0};
    EEPROM.get(RECORDPOS, tmp);

    memcpy(_sensor6035.sensor67Value, tmp, sizeof(tmp));

    EEPROM.end();

    // _sensor6035.outputHeader();

    // uint8_t loops = _ForteSetting.parameter.amplification_time;

    // for (size_t i = 0; i < loops; i++) // cnt
    // {
    //     info_display(float(i) * OPTO_INTERVAL / 60000.0); // time
    //     info_display(",");
    //     for (size_t j = 0; j < 10; j++) // LED channel
    //     {
    //         info_display(_sensor6035.calCalibratedValue(j, i));
    //         info_display(",");
    //         delay(1);
    //     }
    //     info_displayln(_ForteSetting.parameter.amplifTemp);
    // }
    // info_displayln("<AmpStart/>");
    // _displayCLD.screen_Result();
    _displayCLD.changeScreen = true;
    _displayCLD.type_infor = escreenResult;
    return true;
}

/// @brief Restart the system
/// "Res" will restart the device
/// @return System restart
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

void turn_on_led()
{
    int time_delay = 1000 / 2;
    info_displayln("LED on");
    delay(time_delay);
    info_displayln("LED off");
    delay(time_delay);
}

bool ForteSetting::start_amplification_simulation()
{
    if (strncasecmp(recvData, "Amplify", 7))
    {
        return false;
    }
    // Initialize a float variable "counter" with a value of 0
    float counter = 0;
    volatile int pass;

    // Initialize an unsigned long variable "duration" and "interval"
    unsigned long interval = 10 * 1000;
    unsigned long duration = interval * 30;

    // Initialize an unsigned long variable "start_time" and "start_duration"
    unsigned long start_time = 0;
    unsigned long start_duration = 0;

    ; // Initialize an integer variable "time_delay", "time_delay_test",
    // "temp_delay"
    int time_delay = 1000;
    int time_delay_test = 4000;

    const float count_time[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
                                10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
                                20, 21, 22, 23, 24, 25, 26, 27, 28, 29};

    const float count_1[] = {468.06, 775.39, 782.2, 800.52, 795.81, 785.86,
                             797.38, 791.1, 796.86, 798.43, 797.38, 793.19,
                             789.53, 805.24, 790.05, 808.38, 792.67, 796.86,
                             800, 792.15, 802.62, 792.15, 800.52, 787.96,
                             806.81, 786.91, 807.33, 794.76, 801.57, 788.48};

    const float count_2[] = {835.82, 1189.55, 1192.54, 1216.42, 1211.94, 1204.48,
                             1216.42, 1214.93, 1219.4, 1225.37, 1225.37, 1229.85,
                             1250.75, 1308.96, 1364.18, 1440.3, 1500, 1544.78,
                             1558.21, 1576.12, 1576.12, 1558.21, 1544.78, 1555.22,
                             1538.81, 1546.27, 1529.85, 1532.84, 1525.37, 1525.37};

    const float count_3[] = {787.5, 1133.33, 1147.92, 1170.83, 1160.42, 1160.42,
                             1166.67, 1168.75, 1168.75, 1177.08, 1170.83, 1177.08,
                             1195.83, 1262.5, 1318.75, 1404.17, 1460.42, 1504.17,
                             1518.75, 1541.67, 1531.25, 1514.58, 1500, 1506.25,
                             1493.75, 1502.08, 1489.58, 1491.67, 1481.25, 1483.33};

    const float count_4[] = {258.26, 680, 692.17, 724.35, 709.57, 709.57,
                             709.57, 717.39, 707.83, 721.74, 708.7, 716.52,
                             723.48, 771.3, 818.26, 890.43, 950.43, 995.65,
                             1013.91, 1027.83, 1025.22, 1006.96, 985.22, 980.87,
                             966.96, 966.96, 956.52, 959.13, 946.96, 948.7};

    const float count_5[] = {635.44, 796.2, 800, 829.11, 817.72, 831.65,
                             829.11, 845.57, 832.91, 853.16, 839.24, 851.9,
                             868.35, 913.92, 965.82, 1031.65, 1092.41, 1127.85,
                             1148.1, 1159.49, 1163.29, 1155.7, 1141.77, 1145.57,
                             1136.71, 1139.24, 1131.65, 1135.44, 1130.38, 1127.85};

    const float count_6[] = {498.21, 573.21, 591.07, 589.29, 598.21, 591.07,
                             603.57, 589.29, 605.36, 587.5, 601.79, 589.29,
                             601.79, 594.64, 591.07, 596.43, 591.07, 589.29,
                             596.43, 583.93, 592.86, 582.14, 592.86, 583.93,
                             592.86, 583.93, 589.29, 582.14, 580.36, 594.64};

    const float count_7[] = {208.63, 276.98, 292.09, 289.93, 299.28, 291.37,
                             302.16, 292.09, 303.6, 292.81, 302.16, 292.81,
                             304.32, 294.24, 302.16, 295.68, 301.44, 298.56,
                             315.83, 327.34, 374.82, 423.02, 487.77, 529.5,
                             571.22, 593.53, 608.63, 618.71, 628.06, 635.97};

    const float count_8[] = {93.48, 202.17, 239.13, 236.96, 247.83, 232.61,
                             252.17, 234.78, 247.83, 232.61, 245.65, 230.43,
                             245.65, 228.26, 245.65, 234.78, 245.65, 250,
                             280.43, 293.48, 389.13, 484.78, 617.39, 704.35,
                             778.26, 836.96, 854.35, 873.91, 897.83, 908.7};

    const float count_9[] = {748.57, 865.71, 894.29, 891.43, 905.71, 894.29,
                             908.57, 897.14, 908.57, 891.43, 902.86, 891.43,
                             911.43, 897.14, 908.57, 897.14, 902.86, 902.86,
                             934.29, 957.14, 1048.57, 1137.14, 1234.29, 1305.71,
                             1368.57, 1397.14, 1408.57, 1425.71, 1445.71, 1442.86};

    const float count_10[] = {727.5, 850, 872.5, 862.5, 875, 865, 875, 865,
                              875, 862.5, 877.5, 857.5, 885, 862.5, 887.5, 862.5,
                              885, 880, 920, 955, 1072.5, 1190, 1320, 1415,
                              1495, 1537.5, 1560, 1580, 1590, 1597.5};

    const float count_temp[] = {65.8, 66.0, 65.8, 65.6, 65.8, 65.6, 65.7, 65.7,
                                65.6, 65.4, 65.2, 65.0, 65.3, 65.4, 65.5, 65.5,
                                65.5, 65.3, 65.3, 65.1, 65.1, 65.1, 65.2, 65.0,
                                65.0, 64.9, 65.1, 65.4, 65.3, 65.2};

    info_displayln("<AmpStart>");
    info_displayln("{\"calibration\":{\"slopes\":[1.92,0.68,0.48,1.15,0.8,0.56,1.40,0.46,0."
                   "36,0.4],\"origins\":[-42.25,-31.23,18.65,29.45,29.34,28.41,23.34,1.52,1."
                   "64,-11.68]},\"led_power\":[154,77,77,154,77,77,154,77,90,77],\"units\":"
                   "\"nM FAM\",\"device_id\":\"proto "
                   "1\",\"slots\":10,\"amplification_time\":30,\"measurement_interval\":5,"
                   "\"software_version\":\"0.0\"}");
    // Print Phase 1 LED and ALS start message with process and interval details
    info_displayln("=================================================================");
    info_displayln("Start Phase 1 PID LED and ALS");
    info_display("Duration of process: ");
    info_display(float(duration) / 60000); // Convert millis to minutes
    info_displayln(" minute(s)");
    info_display("Interval of process: ");
    info_display(float(interval) / 60000); // Convert millis to minutes
    info_displayln(" minute(s)");
    info_displayln("=================================================================");

    // Print heading
    info_displayln("Amplification Time[min],Sensor 1 Fluorescence[nM FAM],Sensor 2 "
                   "Fluorescence[nM FAM],Sensor 3 Fluorescence[nM FAM],Sensor 4 "
                   "Fluorescence[nM FAM],Sensor 5 Fluorescence[nM FAM],Temperature[C]");
    start_time = start_duration = millis();
    while (millis() - start_duration < duration)
    {
        while (counter == 0 || millis() - start_time >= interval)
        {
            start_time = millis();
            info_display(count_time[(int)counter]);
            info_display(",");
            turn_on_led();
            info_display(count_1[(int)counter]);
            info_display(",");
            turn_on_led();
            info_display(count_2[(int)counter]);
            info_display(",");
            turn_on_led();
            info_display(count_3[(int)counter]);
            info_display(",");
            turn_on_led();
            info_display(count_4[(int)counter]);
            info_display(",");
            turn_on_led();
            info_display(count_5[(int)counter]);
            info_display(",");
            turn_on_led();
            info_display(count_6[(int)counter]);
            info_display(",");
            turn_on_led();
            info_display(count_7[(int)counter]);
            info_display(",");
            turn_on_led();
            info_display(count_8[(int)counter]);
            info_display(",");
            turn_on_led();
            info_display(count_9[(int)counter]);
            info_display(",");
            turn_on_led();
            info_display(count_10[(int)counter]);
            info_display(",");
            info_displayln(count_temp[(int)counter]);
            counter++;
        }
    }
    // Reset variables
    start_time = start_duration = counter = 0;
    return true;
}

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

ForteSetting::ForteSetting(/* args */)
{
}

ForteSetting::~ForteSetting()
{
}

void ForteSetting::begin()
{
    parastructure paraEEPROM;
    EEPROM.begin(_EEPROM_SIZE);
    EEPROM.get(PARAMETERPOS, paraEEPROM);
    info_displayf("check para in EEPROM, length is %d\n", paraEEPROM.length);
    if (paraEEPROM.length == sizeof(parameter)) // if the length of the parameter in EEPROM is not -1 or 0, then use it.
    {
        info_displayf("there is para in the EEPROM with length %d\n", sizeof(parameter));
        parameter = paraEEPROM;
        paraDisplay(parameter);
    }
    else
    {
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
void ForteSetting::loop()
{
    if (Serial.available() > 0)
    {
        info_displayln("data received from Serial port");
        recvLen = 0;
        recvTime = millis();

        while (millis() < recvTime + 30)
        {
            while (Serial.available() > 0)
            {
                uint16_t len = Serial.readBytes(recvData + recvLen, 1024 * 2);
                recvTime = millis(); // set receive time first

                if (!moreMsg)
                {
                    if (recvData[0] == '{')
                    {
                        if (recvData[len + recvLen - 1] == '@')
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
                else if (recvData[len + recvLen - 1] == '@') // finish receiving
                {
                    moreMsg = false;
                    len--; // remove the end character '@'
                    recvTime = 0;
                    info_displayln("\nLong json data finished, process it now");
                }
                else
                {
                    recvTime += 10000; // wait for 10s
                    info_displayln("\nLong json data continue receiving, please send next one in 10s");
                }
                recvLen += len;
                if (recvLen >= 1024 * 2)
                {
                    recvLen = 0;
                    recvTime = 0;
                    info_displayln("The cmd is too long, please send it again");
                    while (Serial.available() > 0)
                    {
                        Serial.readBytes(recvData, 1024 * 2);
                    }
                    return;
                }
            }
        }
        recvData[recvLen] = '\0';
        // info_displayln(recvData);
    }
    else if (SerialBT.available() > 0)
    {
        info_displayln("data received from BT");
        recvLen = 0;
        recvTime = millis();
        while (millis() < recvTime + 100)
        {
            while (SerialBT.available() > 0)
            {
                uint16_t len = SerialBT.readBytes(recvData + recvLen, 1024 * 2);
                recvTime = millis(); // set receive time first
                if (!moreMsg)
                {
                    if (recvData[0] == '{')
                    {
                        if (recvData[len + recvLen - 1] == '@')
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
                else if (recvData[len + recvLen - 1] == '@') // finish receiving
                {
                    moreMsg = false;
                    len--; // remove the end character '@'
                    recvTime = 0;
                    info_displayln("\nLong json data finished, process it now");
                }
                else
                {
                    recvTime += 10000; // wait for 10s
                    info_displayln("\nLong json data continue receiving, please send next one in 10s");
                }
                recvLen += len;
                if (recvLen >= 1024 * 2) // support to receive up to 4K data
                {
                    recvLen = 0;
                    recvTime = 0;
                    info_displayln("The cmd is too long, please send it again");
                    while (SerialBT.available() > 0)
                    {
                        SerialBT.readBytes(recvData, 1024 * 2);
                    }
                    return;
                }
            }
        }
        recvData[recvLen] = '\0';
        // info_display(recvData);
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
        // else if (start_amplification_simulation())
        // {}
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

void ForteSetting::rerun()
{
    _PIDControl.rerun();
    _displayCLD.rerun();
    _sensor6035.rerun();
}

ForteSetting _ForteSetting;
