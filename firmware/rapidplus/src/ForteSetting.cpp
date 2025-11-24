/*
Support parameter configuration by serial communication
To receive the full command, here will wait 10ms after receiving, if there is no further data, then stop to process the data
*/

#include "ForteSetting.h"
#include "Bluetooth.h"
#include <vector>

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

void setData(const std::vector<double> &data, uint8_t loop)
{
    const std::vector<double> tmp;
    // tmp.resize(loop);
    // Word tmp[10 * 130] = {0};

    // data.resize(loop);
    // Word count_1[] = {443, 579, 630, 645, 651, 656, 654, 657, 654, 655, 657, 656, 660, 658, 660, 656, 660, 663, 663, 664, 664, 663, 663, 664, 667, 669, 671, 675, 675, 686, 689, 688, 694, 700, 708, 714, 722, 730, 736, 741, 752, 760, 770, 776, 786, 792, 801, 809, 821, 828, 838, 845, 851, 859, 865, 872, 873, 880, 881, 887, 889, 887, 893, 894, 898, 903, 900, 902, 899, 907, 902, 904, 904, 901, 907, 903, 904, 905, 904, 903, 904, 903, 905, 905, 904, 905, 907, 905, 905, 907, 904, 907, 901, 905, 907, 912, 904, 909, 911, 905, 910, 906, 911, 908, 911, 909, 908, 914, 912, 913, 914, 913, 916, 914, 914, 912, 914, 915, 917, 919};
    // Word count_2[] = {365, 523, 615, 647, 659, 664, 664, 662, 660, 664, 662, 664, 662, 662, 663, 662, 664, 662, 662, 662, 666, 663, 666, 665, 667, 667, 669, 666, 665, 669, 672, 671, 670, 671, 671, 673, 673, 672, 671, 672, 675, 678, 679, 679, 678, 675, 678, 678, 678, 679, 679, 679, 683, 680, 682, 684, 683, 685, 687, 686, 684, 685, 686, 688, 686, 685, 685, 687, 686, 686, 682, 686, 687, 688, 687, 690, 693, 686, 688, 689, 688, 687, 687, 692, 695, 692, 695, 694, 691, 692, 694, 691, 695, 692, 694, 694, 697, 697, 695, 689, 693, 692, 693, 694, 695, 695, 695, 694, 694, 696, 695, 699, 694, 696, 696, 694, 696, 698, 700, 696};
    // Word count_3[] = {535, 636, 707, 728, 740, 744, 745, 744, 746, 750, 753, 750, 750, 753, 753, 755, 754, 759, 760, 762, 762, 764, 767, 768, 767, 768, 771, 773, 773, 773, 777, 775, 776, 781, 782, 782, 782, 783, 784, 788, 789, 786, 787, 791, 791, 791, 795, 791, 795, 794, 798, 800, 798, 796, 801, 803, 802, 803, 807, 807, 805, 806, 807, 809, 811, 813, 813, 812, 815, 818, 816, 817, 818, 817, 815, 820, 822, 825, 824, 825, 823, 826, 878, 879, 881, 879, 876, 879, 881, 879, 879, 879, 880, 879, 877, 880, 877, 881, 879, 878, 876, 878, 877, 876, 876, 877, 879, 880, 878, 878, 876, 877, 876, 875, 873, 873, 876, 879, 881, 874};
    // Word count_4[] = {400, 517, 628, 671, 691, 698, 703, 704, 710, 707, 708, 712, 707, 707, 708, 710, 712, 712, 713, 715, 712, 716, 714, 715, 718, 717, 719, 720, 719, 719, 722, 721, 726, 723, 724, 723, 723, 720, 724, 726, 724, 725, 726, 727, 728, 729, 733, 732, 730, 730, 732, 733, 734, 732, 734, 735, 733, 734, 735, 741, 735, 734, 735, 736, 739, 740, 741, 742, 740, 740, 745, 744, 741, 743, 745, 746, 746, 748, 747, 747, 750, 749, 752, 748, 750, 748, 752, 753, 751, 751, 757, 754, 753, 757, 754, 754, 752, 753, 754, 752, 754, 753, 756, 757, 754, 758, 757, 756, 757, 759, 758, 759, 759, 761, 759, 758, 760, 759, 763, 759};
    // Word count_5[] = {598, 693, 791, 830, 841, 848, 848, 854, 851, 851, 854, 855, 855, 855, 855, 855, 858, 857, 857, 856, 856, 858, 860, 862, 859, 862, 862, 862, 865, 867, 870, 873, 879, 882, 886, 890, 898, 904, 912, 919, 925, 933, 945, 958, 965, 976, 986, 1003, 1012, 1023, 1031, 1046, 1055, 1067, 1079, 1087, 1097, 1110, 1114, 1128, 1138, 1143, 1152, 1155, 1168, 1167, 1172, 1182, 1180, 1183, 1186, 1185, 1189, 1185, 1186, 1190, 1189, 1185, 1180, 1184, 1183, 1181, 1179, 1181, 1182, 1180, 1175, 1175, 1174, 1174, 1175, 1175, 1171, 1172, 1176, 1175, 1177, 1175, 1176, 1174, 1174, 1175, 1174, 1173, 1175, 1174, 1169, 1174, 1174, 1175, 1173, 1169, 1173, 1170, 1173, 1173, 1171, 1173, 1169, 1172};
    // Word count_6[] = {402, 522, 642, 679, 692, 694, 697, 699, 700, 698, 698, 701, 701, 704, 699, 702, 703, 706, 706, 709, 710, 706, 711, 710, 713, 709, 712, 713, 712, 713, 712, 714, 719, 716, 718, 716, 720, 720, 716, 721, 720, 721, 720, 718, 721, 723, 724, 724, 724, 721, 726, 728, 727, 729, 725, 726, 729, 726, 727, 727, 725, 732, 726, 725, 728, 728, 727, 726, 729, 733, 730, 730, 731, 730, 728, 732, 732, 732, 730, 736, 733, 734, 731, 735, 733, 735, 735, 737, 734, 735, 735, 740, 739, 736, 738, 738, 737, 740, 738, 739, 740, 740, 743, 741, 742, 739, 743, 743, 743, 742, 742, 743, 744, 742, 742, 744, 745, 746, 748, 748};
    // Word count_7[] = {720, 802, 976, 1047, 1071, 1077, 1083, 1082, 1084, 1086, 1084, 1080, 1082, 1080, 1080, 1080, 1079, 1079, 1076, 1077, 1076, 1079, 1078, 1074, 1080, 1078, 1077, 1078, 1078, 1080, 1076, 1076, 1077, 1077, 1075, 1074, 1078, 1077, 1076, 1073, 1075, 1076, 1074, 1078, 1076, 1072, 1077, 1076, 1074, 1074, 1073, 1079, 1078, 1076, 1077, 1074, 1076, 1077, 1075, 1075, 1074, 1078, 1076, 1077, 1074, 1076, 1079, 1079, 1078, 1079, 1078, 1078, 1075, 1077, 1079, 1077, 1075, 1078, 1079, 1076, 1078, 1077, 1074, 1076, 1079, 1076, 1079, 1078, 1075, 1078, 1079, 1079, 1079, 1081, 1080, 1078, 1079, 1078, 1078, 1078, 1079, 1079, 1078, 1077, 1077, 1078, 1077, 1078, 1077, 1075, 1076, 1079, 1078, 1080, 1078, 1079, 1080, 1080, 1079, 1079};
    // Word count_8[] = {586, 680, 779, 808, 822, 827, 829, 829, 833, 832, 830, 838, 835, 834, 835, 835, 838, 838, 836, 837, 837, 837, 839, 842, 843, 845, 845, 844, 846, 847, 848, 853, 855, 855, 861, 862, 866, 871, 877, 881, 886, 889, 897, 905, 910, 917, 925, 934, 940, 957, 967, 979, 989, 1002, 1016, 1029, 1039, 1051, 1064, 1071, 1081, 1089, 1097, 1106, 1111, 1111, 1115, 1118, 1120, 1119, 1123, 1125, 1127, 1125, 1125, 1128, 1129, 1123, 1125, 1125, 1127, 1126, 1121, 1124, 1121, 1122, 1123, 1123, 1121, 1121, 1125, 1123, 1122, 1117, 1117, 1118, 1118, 1121, 1118, 1118, 1113, 1116, 1122, 1117, 1117, 1113, 1114, 1114, 1114, 1112, 1115, 1111, 1112, 1109, 1107, 1112, 1107, 1111, 1106, 1107};
    // Word count_9[] = {354, 419, 533, 583, 602, 614, 618, 619, 623, 623, 620, 620, 623, 625, 622, 627, 620, 626, 627, 626, 630, 631, 631, 630, 632, 632, 636, 639, 639, 638, 639, 639, 642, 640, 639, 645, 644, 641, 645, 645, 647, 648, 645, 644, 649, 650, 650, 649, 649, 650, 653, 654, 655, 654, 656, 656, 657, 657, 657, 656, 656, 658, 660, 660, 658, 660, 663, 659, 662, 662, 663, 660, 664, 662, 665, 664, 664, 666, 664, 667, 662, 670, 668, 669, 669, 671, 672, 672, 673, 672, 671, 672, 675, 676, 675, 676, 674, 676, 676, 674, 678, 679, 679, 680, 678, 680, 683, 679, 683, 683, 680, 684, 684, 682, 685, 687, 686, 688, 686, 687};
    // Word count_10[] = {340, 469, 570, 602, 610, 612, 616, 618, 614, 616, 617, 615, 613, 616, 616, 616, 617, 617, 619, 620, 620, 619, 621, 620, 622, 619, 621, 623, 623, 622, 622, 623, 625, 631, 630, 632, 631, 637, 640, 644, 650, 658, 669, 675, 688, 695, 704, 715, 728, 737, 749, 760, 772, 782, 798, 806, 822, 829, 840, 852, 863, 868, 874, 883, 886, 890, 895, 898, 899, 902, 899, 896, 897, 895, 897, 898, 901, 898, 903, 902, 907, 909, 904, 904, 906, 905, 905, 903, 906, 903, 902, 906, 905, 903, 904, 903, 903, 905, 903, 903, 904, 906, 901, 898, 899, 895, 898, 901, 900, 899, 898, 897, 900, 895, 898, 895, 892, 893, 893, 892};
    // Word dataSml[10][130] = {count_1, count_2, count_3, count_4, count_5, count_6, count_7, count_8, count_9, count_10};

    // Word dataSml[10][130] = {count_1, count_2, count_3, count_4, count_5, count_6, count_7, count_8, count_9, count_10};
    // count_1[] = {443, 579, 630, 645, 651, 656, 654, 657, 654, 655, 657, 656, 660, 658, 660, 656, 660, 663, 663, 664, 664, 663, 663, 664, 667, 669, 671, 675, 675, 686, 689, 688, 694, 700, 708, 714, 722, 730, 736, 741, 752, 760, 770, 776, 786, 792, 801, 809, 821, 828, 838, 845, 851, 859, 865, 872, 873, 880, 881, 887, 889, 887, 893, 894, 898, 903, 900, 902, 899, 907, 902, 904, 904, 901, 907, 903, 904, 905, 904, 903, 904, 903, 905, 905, 904, 905, 907, 905, 905, 907, 904, 907, 901, 905, 907, 912, 904, 909, 911, 905, 910, 906, 911, 908, 911, 909, 908, 914, 912, 913, 914, 913, 916, 914, 914, 912, 914, 915, 917, 919};
    // count_2[] = {365, 523, 615, 647, 659, 664, 664, 662, 660, 664, 662, 664, 662, 662, 663, 662, 664, 662, 662, 662, 666, 663, 666, 665, 667, 667, 669, 666, 665, 669, 672, 671, 670, 671, 671, 673, 673, 672, 671, 672, 675, 678, 679, 679, 678, 675, 678, 678, 678, 679, 679, 679, 683, 680, 682, 684, 683, 685, 687, 686, 684, 685, 686, 688, 686, 685, 685, 687, 686, 686, 682, 686, 687, 688, 687, 690, 693, 686, 688, 689, 688, 687, 687, 692, 695, 692, 695, 694, 691, 692, 694, 691, 695, 692, 694, 694, 697, 697, 695, 689, 693, 692, 693, 694, 695, 695, 695, 694, 694, 696, 695, 699, 694, 696, 696, 694, 696, 698, 700, 696};
    // count_3[] = {535, 636, 707, 728, 740, 744, 745, 744, 746, 750, 753, 750, 750, 753, 753, 755, 754, 759, 760, 762, 762, 764, 767, 768, 767, 768, 771, 773, 773, 773, 777, 775, 776, 781, 782, 782, 782, 783, 784, 788, 789, 786, 787, 791, 791, 791, 795, 791, 795, 794, 798, 800, 798, 796, 801, 803, 802, 803, 807, 807, 805, 806, 807, 809, 811, 813, 813, 812, 815, 818, 816, 817, 818, 817, 815, 820, 822, 825, 824, 825, 823, 826, 878, 879, 881, 879, 876, 879, 881, 879, 879, 879, 880, 879, 877, 880, 877, 881, 879, 878, 876, 878, 877, 876, 876, 877, 879, 880, 878, 878, 876, 877, 876, 875, 873, 873, 876, 879, 881, 874};
    // count_4[] = {400, 517, 628, 671, 691, 698, 703, 704, 710, 707, 708, 712, 707, 707, 708, 710, 712, 712, 713, 715, 712, 716, 714, 715, 718, 717, 719, 720, 719, 719, 722, 721, 726, 723, 724, 723, 723, 720, 724, 726, 724, 725, 726, 727, 728, 729, 733, 732, 730, 730, 732, 733, 734, 732, 734, 735, 733, 734, 735, 741, 735, 734, 735, 736, 739, 740, 741, 742, 740, 740, 745, 744, 741, 743, 745, 746, 746, 748, 747, 747, 750, 749, 752, 748, 750, 748, 752, 753, 751, 751, 757, 754, 753, 757, 754, 754, 752, 753, 754, 752, 754, 753, 756, 757, 754, 758, 757, 756, 757, 759, 758, 759, 759, 761, 759, 758, 760, 759, 763, 759};
    // count_5[] = {598, 693, 791, 830, 841, 848, 848, 854, 851, 851, 854, 855, 855, 855, 855, 855, 858, 857, 857, 856, 856, 858, 860, 862, 859, 862, 862, 862, 865, 867, 870, 873, 879, 882, 886, 890, 898, 904, 912, 919, 925, 933, 945, 958, 965, 976, 986, 1003, 1012, 1023, 1031, 1046, 1055, 1067, 1079, 1087, 1097, 1110, 1114, 1128, 1138, 1143, 1152, 1155, 1168, 1167, 1172, 1182, 1180, 1183, 1186, 1185, 1189, 1185, 1186, 1190, 1189, 1185, 1180, 1184, 1183, 1181, 1179, 1181, 1182, 1180, 1175, 1175, 1174, 1174, 1175, 1175, 1171, 1172, 1176, 1175, 1177, 1175, 1176, 1174, 1174, 1175, 1174, 1173, 1175, 1174, 1169, 1174, 1174, 1175, 1173, 1169, 1173, 1170, 1173, 1173, 1171, 1173, 1169, 1172};
    // count_6[] = {402, 522, 642, 679, 692, 694, 697, 699, 700, 698, 698, 701, 701, 704, 699, 702, 703, 706, 706, 709, 710, 706, 711, 710, 713, 709, 712, 713, 712, 713, 712, 714, 719, 716, 718, 716, 720, 720, 716, 721, 720, 721, 720, 718, 721, 723, 724, 724, 724, 721, 726, 728, 727, 729, 725, 726, 729, 726, 727, 727, 725, 732, 726, 725, 728, 728, 727, 726, 729, 733, 730, 730, 731, 730, 728, 732, 732, 732, 730, 736, 733, 734, 731, 735, 733, 735, 735, 737, 734, 735, 735, 740, 739, 736, 738, 738, 737, 740, 738, 739, 740, 740, 743, 741, 742, 739, 743, 743, 743, 742, 742, 743, 744, 742, 742, 744, 745, 746, 748, 748};
    // count_7[] = {720, 802, 976, 1047, 1071, 1077, 1083, 1082, 1084, 1086, 1084, 1080, 1082, 1080, 1080, 1080, 1079, 1079, 1076, 1077, 1076, 1079, 1078, 1074, 1080, 1078, 1077, 1078, 1078, 1080, 1076, 1076, 1077, 1077, 1075, 1074, 1078, 1077, 1076, 1073, 1075, 1076, 1074, 1078, 1076, 1072, 1077, 1076, 1074, 1074, 1073, 1079, 1078, 1076, 1077, 1074, 1076, 1077, 1075, 1075, 1074, 1078, 1076, 1077, 1074, 1076, 1079, 1079, 1078, 1079, 1078, 1078, 1075, 1077, 1079, 1077, 1075, 1078, 1079, 1076, 1078, 1077, 1074, 1076, 1079, 1076, 1079, 1078, 1075, 1078, 1079, 1079, 1079, 1081, 1080, 1078, 1079, 1078, 1078, 1078, 1079, 1079, 1078, 1077, 1077, 1078, 1077, 1078, 1077, 1075, 1076, 1079, 1078, 1080, 1078, 1079, 1080, 1080, 1079, 1079};
    // count_8[] = {586, 680, 779, 808, 822, 827, 829, 829, 833, 832, 830, 838, 835, 834, 835, 835, 838, 838, 836, 837, 837, 837, 839, 842, 843, 845, 845, 844, 846, 847, 848, 853, 855, 855, 861, 862, 866, 871, 877, 881, 886, 889, 897, 905, 910, 917, 925, 934, 940, 957, 967, 979, 989, 1002, 1016, 1029, 1039, 1051, 1064, 1071, 1081, 1089, 1097, 1106, 1111, 1111, 1115, 1118, 1120, 1119, 1123, 1125, 1127, 1125, 1125, 1128, 1129, 1123, 1125, 1125, 1127, 1126, 1121, 1124, 1121, 1122, 1123, 1123, 1121, 1121, 1125, 1123, 1122, 1117, 1117, 1118, 1118, 1121, 1118, 1118, 1113, 1116, 1122, 1117, 1117, 1113, 1114, 1114, 1114, 1112, 1115, 1111, 1112, 1109, 1107, 1112, 1107, 1111, 1106, 1107};
    // count_9[] = {354, 419, 533, 583, 602, 614, 618, 619, 623, 623, 620, 620, 623, 625, 622, 627, 620, 626, 627, 626, 630, 631, 631, 630, 632, 632, 636, 639, 639, 638, 639, 639, 642, 640, 639, 645, 644, 641, 645, 645, 647, 648, 645, 644, 649, 650, 650, 649, 649, 650, 653, 654, 655, 654, 656, 656, 657, 657, 657, 656, 656, 658, 660, 660, 658, 660, 663, 659, 662, 662, 663, 660, 664, 662, 665, 664, 664, 666, 664, 667, 662, 670, 668, 669, 669, 671, 672, 672, 673, 672, 671, 672, 675, 676, 675, 676, 674, 676, 676, 674, 678, 679, 679, 680, 678, 680, 683, 679, 683, 683, 680, 684, 684, 682, 685, 687, 686, 688, 686, 687};
    // count_10[] = {340, 469, 570, 602, 610, 612, 616, 618, 614, 616, 617, 615, 613, 616, 616, 616, 617, 617, 619, 620, 620, 619, 621, 620, 622, 619, 621, 623, 623, 622, 622, 623, 625, 631, 630, 632, 631, 637, 640, 644, 650, 658, 669, 675, 688, 695, 704, 715, 728, 737, 749, 760, 772, 782, 798, 806, 822, 829, 840, 852, 863, 868, 874, 883, 886, 890, 895, 898, 899, 902, 899, 896, 897, 895, 897, 898, 901, 898, 903, 902, 907, 909, 904, 904, 906, 905, 905, 903, 906, 903, 902, 906, 905, 903, 904, 903, 903, 905, 903, 903, 904, 906, 901, 898, 899, 895, 898, 901, 900, 899, 898, 897, 900, 895, 898, 895, 892, 893, 893, 892};
    for (size_t i = 0; i < 10; i++)
    {
        for (size_t j = 0; j < loop; j++)
        {
            // _sensor6035.sensor67Value[i][j] = dataSml[i][j];
        }
        Serial.println();
        // sml_recordOut.time_data.assign(sml_dataIn.time_data.begin(), sml_dataIn.time_data.end());
        // sml_recordOut.raw_data.assign(sml_dataIn.raw_data.begin(), sml_dataIn.raw_data.end());
        // post_process_curve(sml_recordOut,
        //                    sml_dataIn.parameters.baseline_start,
        //                    sml_dataIn.parameters.baseline_range,
        //                    sml_dataIn.parameters.sg_window,
        //                    sml_dataIn.parameters.sg_order);
        // differentiate(sml_recordOut.time_data,
        //               sml_recordOut.processed_data,
        //               sml_recordOut.differential_data);

        // find_sigmoidal_feature(sml_recordOut, sml_dataIn.parameters);
        // predict_outcome(sml_recordOut, sml_dataIn.parameters);

        // JsonDocument jsonOut = sml_recordOut.toJSON();
        // delay(100);
        // serializeJson(jsonOut, Serial);
        // sml_recordOut.clear();
    }
}

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
                        else if (recvData[len + recvLen - 1] == '#')
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
                else if (recvData[len + recvLen - 1] == '#') // finish receiving
                {
                    moreMsg = false;
                    len--; // remove the end character '#'
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

void ForteSetting::rerun()
{
    _PIDControl.rerun();
    _displayCLD.rerun();
    _sensor6035.rerun();
}

ForteSetting _ForteSetting;
