#include "PIDControl.h"

#define LYSIS_TEMP _ForteSetting.parameter.lysisTemp
#define AMPLIF_TEMP _ForteSetting.parameter.amplifTemp
#define TOPHEATER2PWMLOW _ForteSetting.parameter.hotlidPWM[0][0]
#define TOPHEATER2PWMHIGH _ForteSetting.parameter.hotlidPWM[0][1]
#define TOPHEATER3PWMLOW _ForteSetting.parameter.hotlidPWM[1][0]
#define TOPHEATER3PWMHIGH _ForteSetting.parameter.hotlidPWM[1][1]
PIDControl::PIDControl(/* args */)
{
    // myPID = new PID(&CURRENT_TEMP_PID, &RESPONSE_SIGNAL, &TARGET_TEMP, Kp, Ki, Kd, DIRECT);
    // myPID.SetMode(AUTOMATIC);
    // myPID2.SetMode(AUTOMATIC);
    // myPID3.SetMode(AUTOMATIC);
}

PIDControl::~PIDControl()
{
}

void PIDControl::begin()
{
    // initialize the PID parameter and varible here
    double Kp = _ForteSetting.parameter.kpid[0];    // 25
    double Ki = _ForteSetting.parameter.kpid[1];    // 0.1
    double Kd = _ForteSetting.parameter.kpid[2];    // 30
    double Kp_2 = _ForteSetting.parameter.kpid2[0]; // 25
    double Ki_2 = _ForteSetting.parameter.kpid2[1]; // 0.1
    double Kd_2 = _ForteSetting.parameter.kpid2[2]; // 30

    if (_ForteSetting.parameter.buzzerOn == 2)
    {
        RESPONSE_SIGNAL = 1.0;
    }
    // PID initialization
    myPID = new PID(&CURRENT_TEMP_PID, &RESPONSE_SIGNAL, &TARGET_TEMP, Kp, Ki, Kd, DIRECT);
    myPID2 = new PID(&CURRENT_TEMP_PID, &RESPONSE_SIGNAL, &TARGET_TEMP, Kp_2, Ki_2, Kd_2, DIRECT);
    myPID3 = new PID(&CURRENT_TEMP_PID, &RESPONSE_SIGNAL, &TARGET_TEMP, Kp_2, Ki_2, Kd_2, DIRECT);

    myPID->SetMode(AUTOMATIC);
    myPID2->SetMode(AUTOMATIC);
    myPID3->SetMode(AUTOMATIC);

    // Heater1
    pinMode(HEATER1IO, OUTPUT);
    analogWrite(HEATER1IO, PWM_OFF);

    // Heater2
    pinMode(HEATER2IO, OUTPUT);
    analogWrite(HEATER2IO, PWM_OFF);

    // Heater3
    pinMode(HEATER3IO, OUTPUT);
    analogWrite(HEATER3IO, PWM_OFF);

    // //Hotlid1
    // pinMode(HOTLID1IO, OUTPUT);
    // analogWrite(HOTLID1IO, PWM_OFF);

    // Hotlid23, PCB V1.2
    pinMode(HOTLID23IO, OUTPUT);
    analogWrite(HOTLID23IO, PWM_OFF);

    // PCB V1.3
    pinMode(HOTLID2IO, OUTPUT);
    analogWrite(HOTLID2IO, PWM_OFF);
    pinMode(HOTLID3IO, OUTPUT);
    analogWrite(HOTLID3IO, PWM_OFF);

    _bottomThermometer.begin();
    _topThermometer.begin();

    info_displayf("The PID para is\nP:%.4g I:%.4g D:%.4g.\nP2:%.4g I2:%.4g D2:%.4g. \n", Kp, Ki, Kd, Kp_2, Ki_2, Kd_2);

    sensorSeq();
}

void PIDControl::loop()
{
#define TIMEOUT 60 * 1000 // time delay sensorTop error
    /* Bottom ***********************************************************************************/
    _bottomThermometer.loop();
    if (!_bottomThermometer.getNewTemperatureFlag()) // if new temperature is not ready, then return directly. here need add the time out process!!!!!!
    {
        // if time out for 5 seconds, then stop all heating for safety concern
        if (bottomSensorRespTime < millis())
        {
            _bottomThermometer.begin();
            _displayCLD.ErrorProcess("No data from\n bottom sensor", "5sec");
            info_displayln("No data from bottom sensor for 5~10sec");
            _PIDControl.rerun();
        }
        return;
    }

    if ((_bottomThermometer.getTempSensorQuantity() != HEATBLKQUANTITY) && (!bheater1Simu))
    {
        info_displayf("Only got %d/3 bottom sensor resp\n", _bottomThermometer.getTempSensorQuantity());
        // if time out for 10 seconds, then stop all heating for safety concern
        if (bottomSensorRespTime < millis())
        {
            _bottomThermometer.begin();
            _displayCLD.ErrorProcess("Not all data from\n bottom sensors", "5sec");
            info_displayln("Not all data from bottom sensors for 5~10sec");
            // _ForteSetting.rerun();
            _PIDControl.rerun();
        }
        return;
    }
    const double targetTemp[HEATBLKQUANTITY] = {LYSIS_TEMP + 10, AMPLIF_TEMP + 10, AMPLIF_TEMP + 10};
    for (uint8_t i = 0; i < HEATBLKQUANTITY; i++)
    {
        bottomTemperature[i] = _bottomThermometer.getTemperature()[(int)_ForteSetting.parameter.bottomTemperatureSensorSq[i]] + _ForteSetting.parameter.temperatureOffset[i];
        if (targetTemp[i] < bottomTemperature[i])
        {
            stopAllHeating();
            _displayCLD.ErrorProcess("Heater" + String(i + 1) + " is too hot", String(bottomTemperature[i]));
            info_displayf("Heater%d is too hot. T %.4g\n", i + 1, bottomTemperature[i]);
            _PIDControl.rerun();
            return;
        }
        if (bottomTemperature[i] == -127.0 + _ForteSetting.parameter.temperatureOffset[i])
        {
            if (bottomSensorRespTime < millis())
            {
                _bottomThermometer.begin();
                _displayCLD.ErrorProcess("Wrong data from\n bottom sensor", "5sec");
                info_displayln("Wrong data from bottom sensor for 5~10sec");
                _PIDControl.rerun();
            }
            info_displayln("Bottom heater is disconnected");
            info_displayf("\nbottom heater: %.4g:%.4g:%.4g\n", bottomTemperature[0], bottomTemperature[1], bottomTemperature[2]);
            return;
        }
    }
    bottomSensorRespTime = millis() + TIMEOUT;

    /* Top **********************************************************************************/
    _topThermometer.loop(); // low priority
    if (!_topThermometer.getNewTemperatureFlag())
    {
        // if time out for 5 seconds, then stop all heating for safety concern
        if (topSensorRespTime < millis())
        {
            _topThermometer.begin();
            _displayCLD.ErrorProcess("No data from\n top sensor", "5sec");
            info_displayln("No data from top sensor for 5~10sec");
            // _PIDControl.rerunPIDTop();
            _PIDControl.rerun();
        }
        return;
    }

    // Check number of sensors connected to the top heater
    if ((_topThermometer.getTempSensorQuantity() != HOTLIDQUANTITY) && (!bheater1Simu))
    {
        info_displayf("Only got %d/3 top sensor resp\n", _topThermometer.getTempSensorQuantity());
        // if time out for 10 seconds, then stop all heating for safety concern
        if (topSensorRespTime < millis())
        {
            _topThermometer.begin();
            _displayCLD.ErrorProcess("No data from\n top sensor", "5sec");
            info_displayln("No all data from top sensor for 5~10sec");
            // _PIDControl.rerunPIDTop();
            _PIDControl.rerun();
        }
        return;
    }
    const double targetTempHotlid[HOTLIDQUANTITY] = {HOTLID23_TEMP + OVERHEAT_THRESHOLD_TOP2, // Hotlid 2
                                                     HOTLID23_TEMP + OVERHEAT_THRESHOLD_TOP3, // Hotlid 3
                                                     50 + OVERHEAT_THRESHOLD_TOP2};           // last one is the ambient temperature
    for (uint8_t i = 0; i < HOTLIDQUANTITY; i++)
    {
        HotlidTemperature[i] = _topThermometer.getTemperature()[(int)_ForteSetting.parameter.topTemperatureSensorSq[i]] + _ForteSetting.parameter.temperatureOffset[3 + i];
        if (targetTempHotlid[i] < HotlidTemperature[i])
        {
            stopAllHeating();
            if (i < 2)
            {
                _displayCLD.ErrorProcess("Hotlid" + String(i + 1) + " is too hot", String(HotlidTemperature[i]));
                info_displayf("TopHeater%d is too hot. T:%.4g\n", i + 1, HotlidTemperature[i]);
            }
            else
            {
                _displayCLD.ErrorProcess("Ambient is too hot", String(HotlidTemperature[i]));
                info_displayf("Ambient is too hot. T: %.4g\n", HotlidTemperature[i])
            }
            // _PIDControl.rerunPIDTop();
            _PIDControl.rerun();
            return;
        }

        if (HotlidTemperature[i] == -127.0 + _ForteSetting.parameter.temperatureOffset[i + 3])
        {
            HotlidTemperature[i] = 0.0;
            if (topSensorRespTime < millis())
            {
                _topThermometer.begin();
                _displayCLD.ErrorProcess("Wrong data from\n top sensor", "5sec");
                info_displayln("Wrong data from top sensor for 5~10sec");
                _PIDControl.rerun();
                // _PIDControl.rerunPIDTop();
            }
            info_displayln("Top heater is disconnected");
            info_displayf("\nTop heater: %.4g:%.4g:%.4g\n", HotlidTemperature[0], HotlidTemperature[1], HotlidTemperature[2]);
            return;
        }
    }
    topSensorRespTime = millis() + TIMEOUT;

    if (btemperatureOut)
    {
        info_displayf("\nTimeRT\t%.2f\tTopHeater\tReading\t%.4g\t%.4g\t%.4g\n", millis() / 1000.0, HotlidTemperature[0], HotlidTemperature[1], HotlidTemperature[2]);
        info_displayf("\nTimeRB\t%.2f\tBottomHeater\tReading\t%.4g\t%.4g\t%.4g\n", millis() / 1000.0, bottomTemperature[0], bottomTemperature[1], bottomTemperature[2]);
    }

    // start to process after receive new tempeature
    _bottomThermometer.clearNewTemperatureFlag(); // this may need to be clear after the process?

    if (_displayCLD.temperatureShow) // if the screen show the tempeature now, then update the new tempeature value
    {
        /* code */
        _displayCLD.changeScreen = true; // only change when there is temperature display
    }

    switch (pidStep)
    {
    case epidready: // get ready, do nothing until user start the testing by press green button
        return;
    case epid1startpreHeat80:
        StartPreheat80();
        break;
    case epid1preheat80:
        Heat1Preheat80();
        break;
        // case epid1hotlid:
        //     pid1Maintain80(); // maintain the heater1 to be 80 degree when heat up hotlid
        //     // HeatHotlid1();
    case epid1ready: // pid1 is ready, wait user to put lysis tube, continue at maintain 80
        pid1Maintain80();
        break;
        // MaintainHotlid1();
    // case epid1maintain80:       //maintain, but no need change status, previous one should be enough
    //     pid1Maintain80();
    //     MaintainHotlid1();
    //     break;
    case epid2startpreHeat67:
    {
        StartPreheat2_67();
        break;
    }
    case epid2preHeat67:
    {
        Preheat2_67();
        break;
    }
    case epid3startpreHeat67:
    {
        Maintain2_67();
        StartPreheat3_67();
        break;
    }
    case epid3preHeat67:
    {
        Maintain2_67();
        Preheat3_67();
        break;
    }
    case ehotlid23heat:
    {
        Maintain2_67();
        Maintain3_67();
        HeatHotlid23();
        break;
    }
    case epid23ready: // wait for user press button and finish measurement
    {
        Maintain2_67();
        Maintain3_67();
        MaintainHotlid23();
        break;
    }
    default:
        break;
    }
}

void PIDControl::rerun()
{
    // begin();
    stopAllHeating();
    delay(110);
    // clear the Isum in the PID
    double Ki = _ForteSetting.parameter.kpid[1];    // 0.1
    double Ki_2 = _ForteSetting.parameter.kpid2[1]; // 0.1
    CURRENT_TEMP_PID = TARGET_TEMP + 255 * 10 / Ki;
    myPID->Compute();
    CURRENT_TEMP_PID = TARGET_TEMP + 255 * 10 / Ki_2;
    myPID2->Compute();
    myPID3->Compute();
    delay(120);
    CURRENT_TEMP_PID = TARGET_TEMP;
    myPID->Compute();
    CURRENT_TEMP_PID = TARGET_TEMP;
    myPID2->Compute();
    myPID3->Compute();
    pidStep = epidready;
}

void PIDControl::rerunPIDBottom(void)
{
    stopHeaterBottom();
    delay(110);
    // clear the Isum in the PID
    double Ki = _ForteSetting.parameter.kpid[1];    // 0.1
    double Ki_2 = _ForteSetting.parameter.kpid2[1]; // 0.1
    CURRENT_TEMP_PID = TARGET_TEMP + 255 * 10 / Ki;
    myPID->Compute();
    CURRENT_TEMP_PID = TARGET_TEMP + 255 * 10 / Ki_2;
    myPID2->Compute();
    myPID3->Compute();
    delay(120);
    CURRENT_TEMP_PID = TARGET_TEMP;
    myPID->Compute();
    CURRENT_TEMP_PID = TARGET_TEMP;
    myPID2->Compute();
    myPID3->Compute();
}

void PIDControl::rerunPIDTop(void)
{
    stopHeaterTop();
    delay(110);
}

void PIDControl::sensorSeq()
{
    // check the setting of sensor
    uint8_t *bottomsensor = _ForteSetting.parameter.bottomTemperatureSensorSq;
    // Find the seq of bottom heater
    if ((bottomsensor[0] + bottomsensor[1] + bottomsensor[2]) == 0) // simply check whther the sequence of bottom heater is set or not
    {
        // not initialized yet, need to find the right sequence
        _displayCLD.TemperatureBottomSeqDisplay();

        // process bottom first
        const uint8_t bottomHeaterPin[3] = {HEATER1IO, HEATER2IO, HEATER3IO};
        // step 1. Read all temperature
        while (!_bottomThermometer.getNewTemperatureFlag())
        {
            _ForteSetting.loop();
            _bottomThermometer.loop(); // wait until the temperature reading
            if (_PIDControl.bheater1Simu)
            {
                return;
            }
            if (!digitalRead(BUTTON_WHITE))
            {
                delay(300);
                if (!digitalRead(BUTTON_WHITE))
                {
                    _PIDControl.heatSimulation(0xFF);
                    stopAllHeating();
                    return;
                }
            }
        }
        if (_bottomThermometer.getTempSensorQuantity() != 3)
        {
            _bottomThermometer.begin();
            _displayCLD.ErrorProcess("Bottom sensors\n connect error", String(_bottomThermometer.getTempSensorQuantity()));
            // _displayCLD.type_infor = escreenStart;
            _PIDControl.rerun();
            return;
        }

        _bottomThermometer.clearNewTemperatureFlag();

        double bottomStartTemperature[3] = {100.0, 100.0, 100.0};
        memcpy(bottomStartTemperature, _bottomThermometer.getTemperature(), 3 * sizeof(double));
        info_displayf("bottom start temp: %.2f:%.2f:%.2f\n", bottomStartTemperature[0], bottomStartTemperature[1], bottomStartTemperature[2]);
        for (uint8_t i = 0; i < 3; i++)
        {
            // step 2. heat up
            analogWrite(bottomHeaterPin[i], PWM_FULL);

            delay(800);

            // step 3. read all temperature, found the right one
            _ForteSetting.parameter.bottomTemperatureSensorSq[i] = 0xFF;
            while (_ForteSetting.parameter.bottomTemperatureSensorSq[i] == 0xFF)
            {
                // delay(500);
                while (!_bottomThermometer.getNewTemperatureFlag())
                {
                    _bottomThermometer.loop(); // wait until the temperature reading
                    _ForteSetting.loop();
                    if (_PIDControl.bheater1Simu)
                    {
                        return;
                    }
                    if (!digitalRead(BUTTON_WHITE))
                    {
                        delay(300);
                        if (!digitalRead(BUTTON_WHITE))
                        {
                            _PIDControl.heatSimulation(0xFF);
                            stopAllHeating();
                            return;
                        }
                    }
                }
                _bottomThermometer.clearNewTemperatureFlag();
                _displayCLD.TemperatureBottomSeqDisplay();
                // check all 3 sensors, to get the right one with temperature higher than x degree
                uint8_t seq = 0;
                for (uint8_t j = 0; j < 3; j++)
                {
                    uint8_t k = 0;
                    for (; k < i; k++)
                    {
                        if (j == _ForteSetting.parameter.bottomTemperatureSensorSq[k])
                        {
                            break;
                        }
                    }
                    if (k < i) // if used already, then skip
                    {
                        continue;
                    }

                    info_displayf("compare temp:%f, start temp:%f at %d\n", _bottomThermometer.getTemperature()[j], bottomStartTemperature[j] + 2, j);
                    if (_bottomThermometer.getTemperature()[j] > bottomStartTemperature[j] + 3) // if temperature increased 3 degree, found it
                    {
                        // step 4. stop heat
                        analogWrite(bottomHeaterPin[i], PWM_OFF);
                        stopAllHeating();
                        // record it
                        _ForteSetting.parameter.bottomTemperatureSensorSq[i] = j;
                        // info_displayf("found %d bottom sensor @ %d\n", i, j);
                        break;
                    }
                }
            }
        }
    }

    // Find the seq of top heater
    uint8_t *topsensor = _ForteSetting.parameter.topTemperatureSensorSq;
    if ((topsensor[0] + topsensor[1]) == 0) // simply check whther the sequence of top heater is set or not
    {
        // process bottom first
        uint8_t topHeaterPin[2] = {HOTLID2IO, HOTLID3IO};
        // if (strcmp(_ForteSetting.parameter.PCB_version, "V1.3") < 0) // if it's old PCB version
        // {
        //     topHeaterPin[1] = HOTLID23IO;
        //     topHeaterPin[2] = HOTLID23IO;
        // }
        _displayCLD.TemperatureTopSeqDisplay();

        // step 1. Read all temperature
        while (!_topThermometer.getNewTemperatureFlag())
        {
            _topThermometer.loop(); // wait until the temperature reading
            _ForteSetting.loop();
            if (_PIDControl.bheater1Simu)
            {
                return;
            }
            if (!digitalRead(BUTTON_WHITE))
            {
                delay(100);
                if (!digitalRead(BUTTON_WHITE))
                {
                    _PIDControl.heatSimulation(0xFF);
                    stopAllHeating();
                    return;
                }
            }
        }
        if (_topThermometer.getTempSensorQuantity() != 3)
        {
            _topThermometer.begin();
            _displayCLD.ErrorProcess("Top sensors\n connect error", String(_topThermometer.getTempSensorQuantity()));
            // _displayCLD.type_infor = escreenStart;
            _PIDControl.rerun();
            // delay(5000);
            return;
        }
        _topThermometer.clearNewTemperatureFlag();
        double topStartTemperature[3] = {100.0, 100.0, 100.00};

        memcpy(topStartTemperature, _topThermometer.getTemperature(), 3 * sizeof(double));

        info_displayf("top start temp: %.2f:%.2f:%.2f\n", topStartTemperature[0], topStartTemperature[1], topStartTemperature[2]);
        uint8_t sumSeq = 0;             // this is used to find the sequence of ambient sensor
        for (uint8_t i = 0; i < 2; i++) // control the 2 top heaters to find its sequence
        {
            // step 2. heat up
            analogWrite(topHeaterPin[i], PWM_FULL);

            delay(800);

            // step 3. read all temperature, found the right one
            _ForteSetting.parameter.topTemperatureSensorSq[i] = 0xFF;
            while (_ForteSetting.parameter.topTemperatureSensorSq[i] == 0xFF)
            {
                // delay(1000);
                while (!_topThermometer.getNewTemperatureFlag())
                {
                    _topThermometer.loop(); // wait until the temperature reading
                    _ForteSetting.loop();
                    if (_PIDControl.bheater1Simu)
                    {
                        return;
                    }
                    if (!digitalRead(BUTTON_WHITE))
                    {
                        delay(100);
                        if (!digitalRead(BUTTON_WHITE))
                        {
                            _PIDControl.heatSimulation(0xFF);
                            stopAllHeating();
                            return;
                        }
                    }
                }
                _topThermometer.clearNewTemperatureFlag();
                _displayCLD.TemperatureTopSeqDisplay();
                // check all 3 sensors, to get the right one with temperature higher than x degree
                uint8_t seq = 0, count = 0;
                for (uint8_t j = 0; j < 3; j++)
                {
                    uint8_t seq = 0;
                    uint8_t k = 0;
                    for (; k < i; k++)
                    {
                        if (j == _ForteSetting.parameter.topTemperatureSensorSq[k])
                        {
                            break;
                        }
                    }
                    if (k < i) // if used already, then skip
                    {
                        continue;
                    }

                    info_displayf("compare temp:%f, start temp:%f at %d\n", _topThermometer.getTemperature()[j], topStartTemperature[j] + 2, j);
                    if (_topThermometer.getTemperature()[j] > topStartTemperature[j] + 5) // found it
                    {
                        // step 4. stop heat
                        analogWrite(topHeaterPin[i], PWM_OFF);
                        stopAllHeating();
                        // record it
                        _ForteSetting.parameter.topTemperatureSensorSq[i] = j;
                        info_displayf("found %d top sensor @ %d\n", i, j);
                        sumSeq += j; // sum it
                        break;
                    }
                }
            }
        }
        // find the sequence of ambient sensor
        sumSeq = 1 + 2 - sumSeq;
        if (sumSeq > 2 || sumSeq < 0)
        {
            info_displayf("The sequence result is wrong! %d\n", sumSeq);
        }
        _ForteSetting.parameter.topTemperatureSensorSq[2] = sumSeq;
    }
    _ForteSetting.parameter.length = sizeof(_ForteSetting.parameter); // use this to indicate the EEPROM has valid parameter
    EEPROM.begin(_EEPROM_SIZE);
    EEPROM.put(PARAMETERPOS, _ForteSetting.parameter);
    EEPROM.commit();
    EEPROM.end();
}

void PIDControl::timeoutSetting()
{
    bottomSensorRespTime = millis() + 10 * 1000;
    topSensorRespTime = bottomSensorRespTime;
}

void PIDControl::heatSimulation(int type)
{
    if (type & 0x01)
    {
        bheater1Simu = true;
    }
    if (type & 0x02)
    {
        bheater2Simu = true;
    }
    if (type & 0x04)
    {
        bheater3Simu = true;
    }
    // if (type & 0x08)
    // {
    //     bhotlid1Simu = true;
    // }
    if (type & 0x10)
    {
        bhotlid23Simu = true;
    }
}

void PIDControl::RevTemperatureOutput()
{
    btemperatureOut = !btemperatureOut;
}

void PIDControl::setPID23Ready() // this is only used for simulation purpose, to skip certain step
{
    pidStep = epid23ready;
}

void PIDControl::temperatureSimulation(double *temperature, int index, double targetTemp)
{
    // below is to simulate the tempearture ramping up
    temperature[index] = (millis() - START_INTERVAL_TIME) / 50;
    if (temperature[index] > targetTemp)
    {
        temperature[index] = targetTemp + 0.5;
    }
}

double *PIDControl::getBottomTemperature()
{
    return bottomTemperature;
}

// void PIDControl::heatOffset(int value)
// {
//     temperatureOffset = value;
// }

double *PIDControl::getHotlidTemperature()
{
    return HotlidTemperature;
}

// no use
//  void PIDControl::setpid1preheat80()
//  {
//      //change the step to start the preheating after read the temperature
//      pidStep = epid1preheat80;

// }

// prepare before heating to 80, to make sure it's not over heat
void PIDControl::setpid1startpreHeat80()
{
    // change the step to start the preheating after read the temperature
    pidStep = epid1startpreHeat80;
}

// prepare before heating to 67, to make sure it's not over heat
void PIDControl::StartPreheat80()
{
    CURRENT_TEMP_PID = bottomTemperature[0]; // temperatureOffset;
    TARGET_TEMP = LYSIS_TEMP;                // Prepare to heat up to 80 degree
    if (CURRENT_TEMP_PID > TARGET_TEMP + 1)  //!!!too high tempeature process
    {
        analogWrite(HEATER1IO, PWM_OFF);
        info_displayf("heater1 %.2f overheat, wait until it's cool down\n", CURRENT_TEMP_PID);
        return;
    }
    else // temperature is lower than target, start heating now and change to next step to preheat to 67
    {
        //     myPID->Compute();
        //     RESPONSE_SIGNAL = RESPONSE_SIGNAL *1.0;

        // // info_displayf("temp:%f,pwm1 %f:%d\n", CURRENT_TEMP_PID, RESPONSE_SIGNAL, (int)RESPONSE_SIGNAL);
        //     analogWrite(HEATER1IO, (int)RESPONSE_SIGNAL);
        START_INTERVAL_TIME = millis(); // record the start time to heating
        // record the start time for PID adjustment as the input for the PID calculation
        TARGET_TEMP = LYSIS_TEMP;
        pidStep = epid1preheat80;
        info_displayln("status checking before preheat1 is done");
        // info_displayf("got the output of PID is %f\n", RESPONSE_SIGNAL);
        info_displayln("go to the next step");
    }
}

void PIDControl::Heat1Preheat80()
{
    // below is to simulate the tempearture ramping up, comment it when connecting the real heater1
    if (bheater1Simu)
    {
        temperatureSimulation(bottomTemperature, 0, LYSIS_TEMP);
        stopAllHeating();
    }
    CURRENT_TEMP_PID = bottomTemperature[0]; // temperatureOffset;

    // START_INTERVAL_TIME = millis();

    if (CURRENT_TEMP_PID < TARGET_TEMP - DELTA_FULLPWM)
    {
        analogWrite(HEATER1IO, PWM_FULL);
        info_displayf("\nTimePB1\t%.2f\tHeater1\tHeating\tTemperature\t%.2f\tTarget\t%.2f\tPWM\t%d\n", millis() / 1000.0, CURRENT_TEMP_PID, TARGET_TEMP, PWM_FULL);
    }
    else if (CURRENT_TEMP_PID < TARGET_TEMP - DELTA_HALFPWM)
    {
        analogWrite(HEATER1IO, PWM_HALF);
        info_displayf("\nTimePB1\t%.2f\tHeater1\tHeating\tTemperature\t%.2f\tTarget\t%.2f\tPWM\t%d\n", millis() / 1000.0, CURRENT_TEMP_PID, TARGET_TEMP, PWM_HALF);
    }
    else if (CURRENT_TEMP_PID > TARGET_TEMP + 10) // if it's too high
    {
        stopAllHeating();
        _displayCLD.ErrorProcess("Heater1 is too hot", String(CURRENT_TEMP_PID));
        info_displayf("\nTimePB1\t%.2f\tHeater1\tPreheatOverHeat&Rerun\tTemperature\t%.4g\tTarget\t%.2f\tPWM\t0\n", millis() / 1000.0, CURRENT_TEMP_PID, TARGET_TEMP);
        // ESP.restart();
        _PIDControl.rerun();
        // _ForteSetting.rerun();
        return;
    }
    else if ((CURRENT_TEMP_PID < TARGET_TEMP - 1) || (CURRENT_TEMP_PID > TARGET_TEMP + 1)) // if it's too low or too high
    {
        // Compute PID and adjust the response signal
        myPID->Compute();
        RESPONSE_SIGNAL = RESPONSE_SIGNAL * 1.0;
        analogWrite(HEATER1IO, (int)RESPONSE_SIGNAL);
        // digitalWrite(HEATER1IO, HIGH);
        info_displayf("\nTimePB1\t%.2f\tHeater1\tHeating\tTemperature\t%.2f\tTarget\t%.2f\tPWM\t%d\n", millis() / 1000.0, CURRENT_TEMP_PID, TARGET_TEMP, (int)RESPONSE_SIGNAL);
    }
    else
    { // if the temperature is between -1~+1

        // Compute PID and adjust the response signal
        myPID->Compute();
        RESPONSE_SIGNAL = RESPONSE_SIGNAL * 1.0;
        analogWrite(HEATER1IO, (int)RESPONSE_SIGNAL);
        // pidStep = epid1hotlid; ///////////////////////
        pidStep = epid1ready;
        _displayCLD.type_infor = ewaitLysisTube;
        _displayCLD.changeScreen = true;

        _buzzer.BuzzerAlert();

        // // info_display("Temperature (C): ");
        // info_display("Temperature (C): ");
        // info_displayln(CURRENT_TEMP_PID);
        // // info_displayln(HotlidTemperature[0]);

        // // Print Phase 1 PID end message
        // info_displayln("=================================================================");
        // info_displayln("End of hotlid1 heat.");

        // // Print time taken
        // info_display("Total time taken: ");
        // info_display((millis() - START_INTERVAL_TIME) / 60000);
        // info_displayln(" minutes");
        // info_displayln("Wait button");
        // info_displayln("=================================================================");
        // START_INTERVAL_TIME = millis();     //record time for hotlid heat up
        // // // _displayCLD.type_infor = ewaitLysisTube;//eprepare;     //display
        // // // _displayCLD.changeScreen = true;
        info_display("Temperature (C): ");
        info_displayln(CURRENT_TEMP_PID);

        // Print Phase 1 PID end message
        info_displayln("=================================================================");
        info_displayln("End of heat1 preheat.");

        // Print time taken
        info_display("Total time taken: ");
        info_display((millis() - START_INTERVAL_TIME) / 60000);
        info_displayln(" minutes");
        // info_displayln("Continue hotlid1 heating");
        info_displayln("=================================================================");
        START_INTERVAL_TIME = millis(); // record time for hotlid heat up
    }
}

// void PIDControl::HeatHotlid1()
// {
//     if (!_topThermometer.getNewTemperatureFlag()) // if new temperature is not ready, then return directly.
//     {
//         return;
//     }
//     _topThermometer.clearNewTemperatureFlag(); // this may need to be clear after the process?
//     if (bhotlid1Simu)
//     {
//         temperatureSimulation(HotlidTemperature, 0, HOTLID1_TEMP);
//         stopAllHeating();
//     }
//     if (HotlidTemperature[0] < HOTLID1_TEMP) // Turn on when tempeature is lower than 90
//     {
//         analogWrite(HOTLID1IO, PWM_FULL); // switch on hotlid1
//         info_displayf("\nTimePT1\t%.2f\tTopHeater1\tHeating\tTemperature\t%.4g\tTarget\t%.4g\tPWM\t255\n", millis() / 1000.0, HotlidTemperature[0], HOTLID1_TEMP);
//         return;
//     }
//     else if (HotlidTemperature[0] > HOTLID1_TEMP + 20) // Turn off when temperature is higher than 100
//     {
//         analogWrite(HOTLID1IO, PWM_OFF); // switch off hotlid1
//         info_displayf("\nTimePT1\t%.2f\tTopHeater1\tHeating\tTemperature\t%.4g\tTarget: %.4g\tPWM\t0\n", millis() / 1000.0, HotlidTemperature[0], HOTLID1_TEMP);
//     }
//     else
//     {
//         analogWrite(HOTLID1IO, TOPHEATER1PWMHIGH);
//         pidStep = epid1ready;
//         _displayCLD.type_infor = ewaitLysisTube;
//         _displayCLD.changeScreen = true;

//         _buzzer.BuzzerAlert();

//         info_display("Temperature (C): ");
//         info_displayln(HotlidTemperature[0]);

//         // Print Phase 1 PID end message
//         info_displayln("=================================================================");
//         info_displayln("End of hotlid1 heat.");

//         // Print time taken
//         info_display("Total time taken: ");
//         info_display((millis() - START_INTERVAL_TIME) / 60000);
//         info_displayln(" minutes");
//         info_displayln("Wait button");
//         info_displayln("=================================================================");
//         // START_INTERVAL_TIME = millis();     //record time for hotlid heat up
//     }
// }

void PIDControl::pid1Maintain80()
{
    if (bheater1Simu)
    {
        bottomTemperature[0] = LYSIS_TEMP + 0.5; // chance may still output 255
        stopAllHeating();
        return;
    }
    CURRENT_TEMP_PID = bottomTemperature[0]; // temperatureOffset;
    TARGET_TEMP = LYSIS_TEMP;
    // if(CURRENT_TEMP_PID > TARGET_TEMP+1)
    // {
    //     analogWrite(HEATER1IO, PWM_OFF);
    //     myPID->Compute();       //PID compute, but don't use the result, to decrease the overheat risk
    //     if (btemperatureOut)
    //     {
    //         info_displayf("\nTimeMB1\t%.2f\tHeater1\tMaintain\tTemperature\t%.4g\tTarget\t%.2f\tPWM\t0\n", millis()/1000.0, CURRENT_TEMP_PID, TARGET_TEMP);
    //     }
    //     return;
    // }
    // else
    if (CURRENT_TEMP_PID > TARGET_TEMP + OVERHEAT_THRESHOLD1)
    {
        stopAllHeating();
        _displayCLD.ErrorProcess("Heater1 over heat", String(CURRENT_TEMP_PID));
        info_displayf("\nTimeMB1\t%.2f\tHeater1\tOverHeat&Rerun\tTemperature\t%.4g\tTarget\t%.2f\tPWM\t0\n", millis() / 1000.0, CURRENT_TEMP_PID, TARGET_TEMP);
        // ESP.restart();      //over heat
        _PIDControl.rerun();
        // _ForteSetting.rerun();
        return;
    }
    else if (CURRENT_TEMP_PID < TARGET_TEMP + UNDERHEAT_THRESHOLD1)
    {
        stopAllHeating();
        _displayCLD.ErrorProcess("Heater1 under heat", String(CURRENT_TEMP_PID));
        info_displayf("\nTimeMB1\t%.2f\tHeater1\tUnderHeat&Rerun\tTemperature\t%.4g\tTarget\t%.2f\tPWM\t0\n", millis() / 1000.0, CURRENT_TEMP_PID, TARGET_TEMP);
        // ESP.restart();      //over heat
        _PIDControl.rerun();
        // _ForteSetting.rerun();
        return;
    }
    myPID->Compute();
    RESPONSE_SIGNAL = RESPONSE_SIGNAL * 1.0;
    analogWrite(HEATER1IO, RESPONSE_SIGNAL);
    if (btemperatureOut)
    {
        info_displayf("\nTimeMB1\t%.2f\tHeater1\tMaintain\tTemperature\t%.4g\tTarget\t%.2f\tPWM\t%d\n", millis() / 1000.0, CURRENT_TEMP_PID, TARGET_TEMP, (int)RESPONSE_SIGNAL);
    }
}

void PIDControl::setPreheat67()
{
    // Stop heater1 and hotlid1
    analogWrite(HEATER1IO, PWM_OFF);
    // digitalWrite(HOTLID1IO, LOW);
    // analogWrite(HOTLID1IO, PWM_OFF); // switch off hotlid1
    // change the step to start the preheating after read the temperature
    pidStep = epid2startpreHeat67;
}

// prepare before heating to 67, to make sure it's not over heat
void PIDControl::StartPreheat2_67()
{
    CURRENT_TEMP_PID = bottomTemperature[1]; // temperatureOffset;         //assume the temperature sensor to be 0: heater1; 1: heater2; 2: heater3
    TARGET_TEMP = AMPLIF_TEMP;               // Prepare to heat up to 67 degree
    if (CURRENT_TEMP_PID > TARGET_TEMP + 1)  //!!!too high tempeature process
    {
        analogWrite(HEATER2IO, PWM_OFF);
        if (CURRENT_TEMP_PID > TARGET_TEMP + 20) // for heat block is 100, for hot lid is 120
        {
            info_displayf("Heater2 is too hot, %d degree\n", CURRENT_TEMP_PID);
            return;
        }

        info_displayf("\nheater2 %.2f overheat, wait until it's cool down\n", CURRENT_TEMP_PID);
        return;
    }
    else // temperature is lower than target, start heating now and change to next step to preheat to 67
    {
        //     myPID2->Compute();
        //     RESPONSE_SIGNAL = RESPONSE_SIGNAL *1.0;
        // // info_displayf("temp:%f,pwm2 %f:%d\n", CURRENT_TEMP_PID, RESPONSE_SIGNAL, (int)RESPONSE_SIGNAL);
        //     analogWrite(HEATER2IO, (int)RESPONSE_SIGNAL);
        START_INTERVAL_TIME = millis(); // record the start time to heating
        // record the start time for PID adjustment as the input for the PID calculation
        TARGET_TEMP = AMPLIF_TEMP;
        pidStep = epid2preHeat67; // change flag to next step
        info_displayln("status checking before preheat2 is done");
        info_displayln("go to the next step");
    }
}

void PIDControl::Preheat2_67()
{
    if (bheater2Simu)
    {
        temperatureSimulation(bottomTemperature, 1, AMPLIF_TEMP);
        stopAllHeating();
    }
    // read the temperature of heater2
    CURRENT_TEMP_PID = bottomTemperature[1]; // temperatureOffset;

    // START_INTERVAL_TIME = millis();
    if (CURRENT_TEMP_PID < TARGET_TEMP - DELTA_FULLPWM)
    {
        analogWrite(HEATER2IO, PWM_FULL);
        // digitalWrite(HEATER1IO, HIGH);
        info_displayf("\nTimePB2\t%.2f\tHeater2\tHeating\tTemperature\t%.2f\tTarget\t%.2f\tPWM\t%d\n", millis() / 1000.0, CURRENT_TEMP_PID, TARGET_TEMP, PWM_FULL);
    }
    else if (CURRENT_TEMP_PID < TARGET_TEMP - DELTA_HALFPWM)
    {
        analogWrite(HEATER2IO, PWM_HALF);
        // digitalWrite(HEATER1IO, HIGH);
        info_displayf("\nTimePB2\t%.2f\tHeater2\tHeating\tTemperature\t%.2f\tTarget\t%.2f\tPWM\t%d\n", millis() / 1000.0, CURRENT_TEMP_PID, TARGET_TEMP, PWM_HALF);
    }

    else if (CURRENT_TEMP_PID > TARGET_TEMP + 20) // if it's too high
    {
        stopAllHeating();
        _displayCLD.ErrorProcess("Heater2 is too hot", String(CURRENT_TEMP_PID));
        info_displayf("\nTimePB2\t%.2f\tHeater2\tPreheatOverHeat&Rerun\tTemperature\t%.4g\tTarget\t%.2f\tPWM\t0\n", millis() / 1000.0, CURRENT_TEMP_PID, TARGET_TEMP);
        // ESP.restart();
        // _ForteSetting.rerun();
        _PIDControl.rerun();
        return;
    }
    else if ((CURRENT_TEMP_PID < TARGET_TEMP - 1) || (CURRENT_TEMP_PID > TARGET_TEMP + 1))
    {
        /* code */
        // Compute PID and adjust the response signal
        myPID2->Compute();
        RESPONSE_SIGNAL = RESPONSE_SIGNAL * 1.0;
        info_displayf("\nTimePB2\t%.2f\tHeater2\tHeating\tTemperature\t%.2f\tTarget\t%.2f\tPWM\t%d\n", millis() / 1000.0, CURRENT_TEMP_PID, TARGET_TEMP, (int)RESPONSE_SIGNAL);
        // info_displayf("temp:%.2f->%.2f,pwm2 %d\n", CURRENT_TEMP_PID, TARGET_TEMP, (int)RESPONSE_SIGNAL);
        analogWrite(HEATER2IO, (int)RESPONSE_SIGNAL);
    }
    else
    { // if the temperature is between -1~+1
        pidStep = epid3startpreHeat67;
        info_display("Temperature (C): ");
        info_displayln(CURRENT_TEMP_PID);

        // Print Phase 1 PID end message
        info_displayln("=================================================================");
        info_displayln("End of Phase 1 PID2");

        // Print time taken
        info_display("Total time taken: ");
        info_display((millis() - START_INTERVAL_TIME) / 60000);
        info_displayln(" minutes");
        info_displayln("=================================================================");
    }
}

void PIDControl::StartPreheat3_67()
{
    CURRENT_TEMP_PID = bottomTemperature[2]; // temperatureOffset;         //assume the temperature sensor to be 0: heater1; 1: heater2; 2: heater3
    TARGET_TEMP = AMPLIF_TEMP;               // Prepare to heat up to 67 degree
    if (CURRENT_TEMP_PID > TARGET_TEMP + 1)  //!!!too high tempeature process
    {
        analogWrite(HEATER3IO, PWM_OFF);
        if (CURRENT_TEMP_PID > AMPLIF_TEMP + 20) // for heat block is 100, for hot lid is 120
        {
            info_displayf("Too hot, %d degree\n", CURRENT_TEMP_PID);
            return;
        }
        info_displayf("\nheater3 %.2f overheat, wait until it's cool down\n", CURRENT_TEMP_PID);
        return;
    }
    else // temperature is lower than target, start heating now and change to next step to preheat to 67
    {
        //     myPID3->Compute();
        //     RESPONSE_SIGNAL = RESPONSE_SIGNAL *1.0;
        // // info_displayf("temp:%f,pwm3 %f:%d\n", CURRENT_TEMP_PID, RESPONSE_SIGNAL, (int)RESPONSE_SIGNAL);
        //     analogWrite(HEATER3IO, (int)RESPONSE_SIGNAL);
        START_INTERVAL_TIME = millis(); // record the start time to heating
        // record the start time for PID adjustment as the input for the PID calculation
        TARGET_TEMP = AMPLIF_TEMP;
        pidStep = epid3preHeat67; // change flag to next step
        info_displayln("status checking before preheat3 is done");
        info_displayln("go to the next step");
    }
}

void PIDControl::Preheat3_67()
{
    if (bheater3Simu)
    {
        temperatureSimulation(bottomTemperature, 2, AMPLIF_TEMP);
        stopAllHeating();
    }
    // below is to simulate the tempearture ramping up
    CURRENT_TEMP_PID = bottomTemperature[2]; // temperatureOffset;

    // START_INTERVAL_TIME = millis();
    if (CURRENT_TEMP_PID < TARGET_TEMP - DELTA_FULLPWM)
    {
        analogWrite(HEATER3IO, PWM_FULL);
        // digitalWrite(HEATER1IO, HIGH);
        info_displayf("\nTimePB3\t%.2f\tHeater3\tHeating\tTemperature\t%.2f\tTarget\t%.2f\tPWM\t%d\n", millis() / 1000.0, CURRENT_TEMP_PID, TARGET_TEMP, PWM_FULL);
    }
    else if (CURRENT_TEMP_PID < TARGET_TEMP - DELTA_HALFPWM)
    {
        analogWrite(HEATER3IO, PWM_HALF);
        // digitalWrite(HEATER1IO, HIGH);
        info_displayf("\nTimePB3\t%.2f\tHeater3\tHeating\tTemperature\t%.2f\tTarget\t%.2f\tPWM\t%d\n", millis() / 1000.0, CURRENT_TEMP_PID, TARGET_TEMP, PWM_HALF);
    }
    else if (CURRENT_TEMP_PID > TARGET_TEMP + 10) // if it's too high
    {
        stopAllHeating();
        _displayCLD.ErrorProcess("Heater3 is too hot", String(CURRENT_TEMP_PID));
        info_displayf("\nTimePB3\t%.2f\tHeater3\tPreheatOverHeat&Rerun\tTemperature\t%.4g\tTarget\t%.2f\tPWM\t0\n", millis() / 1000.0, CURRENT_TEMP_PID, TARGET_TEMP);
        // ESP.restart();
        // _ForteSetting.rerun();
        _PIDControl.rerun();
        return;
    }
    else if ((CURRENT_TEMP_PID < TARGET_TEMP - 1) || (CURRENT_TEMP_PID > TARGET_TEMP + 1))
    {
        /* code */
        // Compute PID and adjust the response signal
        myPID3->Compute();
        RESPONSE_SIGNAL = RESPONSE_SIGNAL * 1.0;
        analogWrite(HEATER3IO, (int)RESPONSE_SIGNAL);
        info_displayf("\nTimePB3\t%.2f\tHeater3\tHeating\tTemperature\t%.2f\tTarget\t%.2f\tPWM\t%d\n", millis() / 1000.0, CURRENT_TEMP_PID, TARGET_TEMP, (int)RESPONSE_SIGNAL);
        // info_displayf("\nHeating heater3, tempearature: %.2f, target: %.2f, PID: %d\n", CURRENT_TEMP_PID, TARGET_TEMP, (int)RESPONSE_SIGNAL);
        // limit PID signal to 70% of totl capacity to avoid power supply from overdemand
        // if (RESPONSE_SIGNAL > 120) {RESPONSE_SIGNAL = 120.0;}
    }
    else
    {                            // if between -1~+1
        pidStep = ehotlid23heat; // Heater2 and heater3 is finished heating up, heating up hotlid next
        // _displayCLD.type_infor = eprepare;     //display
        // _displayCLD.changeScreen = true;
        info_display("Temperature (C): ");
        info_displayln(CURRENT_TEMP_PID);

        // Print Phase 1 PID end message
        info_displayln("=================================================================");
        info_displayln("End of Phase 1 PID3");

        // Print time taken
        info_display("Total time taken: ");
        info_display((millis() - START_INTERVAL_TIME) / 60000);
        info_displayln(" minutes");
        info_displayln("Continue hotlid2&3 heating");
        info_displayln("=================================================================");
        START_INTERVAL_TIME = millis();
    }
}

void PIDControl::Maintain3_67()
{
    if (bheater3Simu)
    {
        bottomTemperature[2] = AMPLIF_TEMP + 0.5;
        stopAllHeating();
        return;
    }
    // TARGET_TEMP
    CURRENT_TEMP_PID = bottomTemperature[2]; // temperatureOffset;
    TARGET_TEMP = AMPLIF_TEMP;
    // if(CURRENT_TEMP_PID > TARGET_TEMP+1)
    // {
    //     analogWrite(HEATER3IO, PWM_OFF);
    //     myPID3->Compute();       //PID compute, but don't use the result, to decrease the overheat risk
    //     if (btemperatureOut)
    //     {
    //         info_displayf("\nTimeMB3\t%.2f\tHeater3\tMaintain\tTemperature\t%.4g\tTarget\t%.2f\tPWM\t0\n", millis()/1000.0, CURRENT_TEMP_PID, TARGET_TEMP);
    //     }
    //     return;
    // }
    // else
    if (CURRENT_TEMP_PID > TARGET_TEMP + OVERHEAT_THRESHOLD3)
    {
        stopAllHeating();
        _displayCLD.ErrorProcess("Heater3 over heat", String(CURRENT_TEMP_PID));
        info_displayf("\nTimeMB3\t%.2f\tHeater3\tOverHeat&Rerun\tTemperature\t%.4g\tTarget\t%.2f\tPWM\t0\n", millis() / 1000.0, CURRENT_TEMP_PID, TARGET_TEMP);
        // ESP.restart();      //over heat
        // _ForteSetting.rerun();
        _PIDControl.rerun();
        return;
    }
    else if (CURRENT_TEMP_PID < TARGET_TEMP + UNDERHEAT_THRESHOLD3)
    {
        stopAllHeating();
        _displayCLD.ErrorProcess("Heater3 under heat", String(CURRENT_TEMP_PID));
        info_displayf("\nTimeMB3\t%.2f\tHeater3\tUnderHeat&Rerun\tTemperature\t%.4g\tTarget\t%.2f\tPWM\t0\n", millis() / 1000.0, CURRENT_TEMP_PID, TARGET_TEMP);
        // ESP.restart();      //over heat
        // _ForteSetting.rerun();
        _PIDControl.rerun();
        return;
    }
    myPID3->Compute();
    RESPONSE_SIGNAL = RESPONSE_SIGNAL * 1.0;
    // if (RESPONSE_SIGNAL > 120.0) {RESPONSE_SIGNAL = 120.0;}
    analogWrite(HEATER3IO, (int)RESPONSE_SIGNAL);
    if (btemperatureOut)
    {
        info_displayf("\nTimeMB3\t%.2f\tHeater3\tMaintain\tTemperature\t%.4g\tTarget\t%.2f\tPWM\t%d\n", millis() / 1000.0, CURRENT_TEMP_PID, TARGET_TEMP, (int)RESPONSE_SIGNAL);
    }
}

void PIDControl::heatOldLid23()
{
    if ((HotlidTemperature[1] > HOTLID23_TEMP + 20) || (HotlidTemperature[2] > HOTLID23_TEMP + 20)) // Turn off when temperature is higher than 90
    {
        analogWrite(HOTLID23IO, PWM_OFF);
        info_displayf("\nTimePT23\t%.2f\tTopHeater2&3\tHeating\tTemperature\t%.2f\t%.2f\tTarget\t%.2f\tPWM\t0\n", millis() / 1000.0, HotlidTemperature[1], HotlidTemperature[2], HOTLID23_TEMP);
        // return;
    }
    else if ((HotlidTemperature[1] < HOTLID23_TEMP) || (HotlidTemperature[2] < HOTLID23_TEMP)) // Turn on when tempeature is lower than 70
    {
        // digitalWrite(HOTLID23IO, HIGH);
        analogWrite(HOTLID23IO, PWM_Heater23);
        info_displayf("\nTimePT23\t%.2f\tTopHeater2&3\tHeating\tTemperature\t%.2f\t%.2f\tTarget\t%.2f\tPWM\t%d\n", millis() / 1000.0, HotlidTemperature[1], HotlidTemperature[2], HOTLID23_TEMP, PWM_Heater23);
        // return;
    }
    else
    {
        pidStep = epid23ready;
        if (_sensor6035.getSensorPreheatReady())
        {
            _displayCLD.type_infor = ewaitampTube;
            _displayCLD.changeScreen = true;
            _buzzer.BuzzerAlert();
        }
        info_displayf("Temperature (C): %.2f:%.2f\n", HotlidTemperature[1], HotlidTemperature[2]);

        // Print Phase 1 PID end message
        info_displayln("=================================================================");
        info_displayln("End of hotlid2&3 heat.");

        // Print time taken
        info_display("Total time taken: ");
        info_display((millis() - START_INTERVAL_TIME) / 60000);
        info_displayln(" minutes");
        info_displayln("Wait button or sensor preheat");
        info_displayln("=================================================================");
    }
    if (((HotlidTemperature[1] < HOTLID23_TEMP) && (HotlidTemperature[2] > HOTLID23_TEMP + 10)) || ((HotlidTemperature[1] > HOTLID23_TEMP + 10) && (HotlidTemperature[2] < HOTLID23_TEMP))) // if temperature difference is too much, then alert
    {
        // digitalWrite(HOTLID23IO, LOW);
        analogWrite(HOTLID23IO, PWM_OFF);
        info_displayf("Temperature difference > 10!!!\nTimePT23\t%.2f\tTopHeater2&3\tHeating\tTemperature\t%.2f\t%.2f\tTarget\t%.2f\tPWM\t0\n", millis() / 1000.0, HotlidTemperature[1], HotlidTemperature[2], HOTLID23_TEMP);
        _buzzer.BuzzerAlarm();
        return;
    }
}

#define INDEX_HOTLID2 0
#define INDEX_HOTLID3 1
#define INDEX_AMBIENT 2

void PIDControl::heatNewLid23()
{
    int topHeater2Flag = 0;                                    // used to check whether top heater 2 reach its target temperature
    int topHeater3Flag = 0;                                    // used to check whether top heater 3 reach its target temperature
    if (HotlidTemperature[INDEX_HOTLID2] > HOTLID23_TEMP + 10) // if heater2 is too high
    {
        topHeater2Flag = 0;
        analogWrite(HOTLID2IO, PWM_OFF);
        info_displayf("TopHeater2 is too hot, tempearature: %.2f, target: %.2f, cooling down\n", HotlidTemperature[INDEX_HOTLID2], HOTLID23_TEMP);
    }
    else if ((HotlidTemperature[INDEX_HOTLID2] < HOTLID23_TEMP)) // if heater2 is too low
    {
        analogWrite(HOTLID2IO, PWM_HOTLIDFULL);
        info_displayf("\nTimePT2\t%.2f\tTopHeater2\tHeating\tTemperature\t%.2f\tTarget\t%.2f\tPWM\t127\n", millis() / 1000.0, HotlidTemperature[INDEX_HOTLID2], HOTLID23_TEMP);
    }
    else if (HotlidTemperature[INDEX_HOTLID2] > HOTLID23_TEMP + 5) // if heater2 is higher than 5 degree, then using low PWM value
    {
        analogWrite(HOTLID2IO, TOPHEATER2PWMLOW);
        topHeater2Flag = 1;
        info_displayf("\nTimePT2\t%.2f\tTopHeater2\tMaintain\tTemperature\t%.2f\tTarget\t%.2f\tPWM\t%d\n", millis() / 1000.0, HotlidTemperature[INDEX_HOTLID2], HOTLID23_TEMP, TOPHEATER2PWMLOW);
    }
    else // if heater2 is at right range
    {
        analogWrite(HOTLID2IO, TOPHEATER2PWMHIGH);
        topHeater2Flag = 1;
        info_displayf("\nTimePT2\t%.2f\tTopHeater2\tMaintain\tTemperature\t%.2f\tTarget\t%.2f\tPWM\t%d\n", millis() / 1000.0, HotlidTemperature[INDEX_HOTLID2], HOTLID23_TEMP, TOPHEATER2PWMHIGH);
    }

    if (HotlidTemperature[INDEX_HOTLID3] > HOTLID23_TEMP + 10) // if heater3 is too high
    {
        topHeater3Flag = 0;
        analogWrite(HOTLID3IO, PWM_OFF);
        info_displayf("TopHeater3 is too hot, tempearature: %.2f, target: %.2f, cooling down\n", HotlidTemperature[INDEX_HOTLID3], HOTLID23_TEMP);
    }
    else if ((HotlidTemperature[INDEX_HOTLID3] < HOTLID23_TEMP)) // if heater3 is too low
    {
        analogWrite(HOTLID3IO, PWM_HOTLIDFULL);
        // info_displayf("\nHeating hotlid3, tempearature: %.2f, target: %.2f, PID: 255\n", HotlidTemperature[2], HOTLID23_TEMP);
        info_displayf("\nTimePT3\t%.2f\tTopHeater3\tHeating\tTemperature\t%.2f\tTarget\t%.2f\tPWM\t127\n", millis() / 1000.0, HotlidTemperature[INDEX_HOTLID3], HOTLID23_TEMP);
    }
    else if (HotlidTemperature[INDEX_HOTLID3] > HOTLID23_TEMP + 5) // if heater3 is higher than 5 degree, then using low PWM value
    {
        analogWrite(HOTLID3IO, TOPHEATER3PWMLOW);
        topHeater3Flag = 1;
        info_displayf("\nTimePT3\t%.2f\tTopHeater3\tMaintain\tTemperature\t%.2f\tTarget\t%.2f\tPWM\t%d\n", millis() / 1000.0, HotlidTemperature[INDEX_HOTLID3], HOTLID23_TEMP, TOPHEATER3PWMLOW);
    }
    else // if heater3 is at right range
    {
        analogWrite(HOTLID3IO, TOPHEATER3PWMHIGH);
        topHeater3Flag = 1;
        info_displayf("\nTimePT3\t%.2f\tTopHeater3\tMaintain\tTemperature\t%.2f\tTarget\t%.2f\tPWM\t%d\n", millis() / 1000.0, HotlidTemperature[INDEX_HOTLID3], HOTLID23_TEMP, TOPHEATER3PWMHIGH);
    }

    // check if both of heater2 and heater3 are at the right range
    if (topHeater2Flag == 1 && topHeater3Flag == 1)
    {
        pidStep = epid23ready;
        if (_sensor6035.getSensorPreheatReady())
        {
            _displayCLD.type_infor = ewaitampTube;
            _displayCLD.changeScreen = true;
            _buzzer.BuzzerAlert();
        }
        info_displayf("Temperature (C): %.2f:%.2f\n", HotlidTemperature[INDEX_HOTLID2], HotlidTemperature[INDEX_HOTLID3]);

        // Print Phase 1 PID end message
        info_displayln("=================================================================");
        info_displayln("End of hotlid2&3 heat.");

        // Print time taken
        info_display("Total time taken: ");
        info_display((millis() - START_INTERVAL_TIME) / 60000);
        info_displayln(" minutes");
        info_displayln("Wait button or sensor preheat");
        info_displayln("=================================================================");
    }
}

void PIDControl::HeatHotlid23()
{
    if (!_topThermometer.getNewTemperatureFlag()) // if new temperature is not ready, then return directly.
    {
        return;
    }
    _topThermometer.clearNewTemperatureFlag(); // this may need to be clear after the process?

    if (bhotlid23Simu)
    {
        temperatureSimulation(HotlidTemperature, 0, HOTLID23_TEMP);
        temperatureSimulation(HotlidTemperature, 0, HOTLID23_TEMP);
        stopAllHeating();
    }
    if (strcmp(_ForteSetting.parameter.PCB_version, "V1.3") < 0) // if it's old PCB version
    {
        heatOldLid23();
    }
    else
    {
        heatNewLid23();
    }
}

// void PIDControl::pid23Maintain67()
// {
// }
void PIDControl::maintainOldLid23()
{
    if ((HotlidTemperature[INDEX_HOTLID2] > HOTLID23_TEMP + 10) || (HotlidTemperature[INDEX_HOTLID3] > HOTLID23_TEMP + 10)) // Turn off when temperature is higher than 90
    {
        analogWrite(HOTLID23IO, PWM_OFF);
        if (btemperatureOut)
        {
            info_displayf("\nTimeMT23\t%.2f\tTopHeater2&3\tMaintain\tTemperature\t%.2f\t%.2f\tTarget\t%.2f\tPWM\t0\n", millis() / 1000.0, HotlidTemperature[1], HotlidTemperature[2], HOTLID23_TEMP);
        }
        return;
    }
    else if ((HotlidTemperature[1] < HOTLID23_TEMP) || (HotlidTemperature[2] < HOTLID23_TEMP)) // Turn on when tempeature is lower than target
    {
        analogWrite(HOTLID23IO, PWM_Heater23);
        if (btemperatureOut)
        {
            info_displayf("\nTimeMT23\t%.2f\tTopHeater2&3\tMaintain\tTemperature\t%.2f\t%.2f\tTarget\t%.2f\tPWM\t%d\n", millis() / 1000.0, HotlidTemperature[1], HotlidTemperature[2], HOTLID23_TEMP, PWM_Heater23);
        }
        // return;
    }
    // if (((HotlidTemperature[1]<HOTLID23_TEMP)&&(HotlidTemperature[2]>HOTLID23_TEMP+10))||((HotlidTemperature[1]>HOTLID23_TEMP+10)&&(HotlidTemperature[2]<HOTLID23_TEMP)))        //if temperature difference is too much, then alert
    // {
    //     analogWrite(HOTLID23IO, PWM_OFF);
    //     if (btemperatureOut)
    //     {
    //         info_displayf("\nTimeMT23\t%.2f\tTopHeater2&3\tMaintain\tTemperature\t%.2f\t%.2f\tTarget\t%.2f\tPWM\t0\n", millis()/1000.0, HotlidTemperature[1], HotlidTemperature[2], HOTLID23_TEMP);
    //     }
    //     _buzzer.BuzzerAlarm();
    //     return;
    // }
    else
    {
        analogWrite(HOTLID23IO, PWM_Heater23 / 2);
        if (btemperatureOut)
        {
            info_displayf("\nTimeMT23\t%.2f\tTopHeater2&3\tMaintain\tTemperature\t%.2f\t%.2f\tTarget\t%.2f\tPWM\t%d\n", millis() / 1000.0, HotlidTemperature[1], HotlidTemperature[2], HOTLID23_TEMP, PWM_Heater23 / 2);
        }
    }
}

void PIDControl::maintainNewLid23()
{
    // process top heater 2
    if (HotlidTemperature[INDEX_HOTLID2] > HOTLID23_TEMP + 10) // if heater2 is too high, in case the PWMLOW is not low enough
    {
        analogWrite(HOTLID2IO, PWM_OFF);
        if (btemperatureOut)
        {
            info_displayf("\nTimeMT2\t%.2f\tTopHeater2\tMaintain\tTemperature\t%.2f\tTarget\t%.2f\tPWM\t0\n", millis() / 1000.0, HotlidTemperature[INDEX_HOTLID2], HOTLID23_TEMP);
        }
    }
    else if (HotlidTemperature[INDEX_HOTLID2] > HOTLID23_TEMP + 5) // if heater2 is at high than 5 degree
    {
        analogWrite(HOTLID2IO, TOPHEATER2PWMLOW);
        if (btemperatureOut)
        {
            info_displayf("\nTimeMT2\t%.2f\tTopHeater2\tMaintain\tTemperature\t%.2f\tTarget\t%.2f\tPWM\t%d\n", millis() / 1000.0, HotlidTemperature[INDEX_HOTLID2], HOTLID23_TEMP, TOPHEATER2PWMLOW);
        }
    }
    else if (HotlidTemperature[INDEX_HOTLID2] < HOTLID23_TEMP - 10) // if heater2 is too low, in case the PWMHIGH is not high enough
    {
        analogWrite(HOTLID2IO, PWM_Heater23);
        if (btemperatureOut)
        {
            info_displayf("\nTimeMT2\t%.2f\tTopHeater2\tMaintain\tTemperature\t%.2f\tTarget\t%.2f\tPWM\t%d\n", millis() / 1000.0, HotlidTemperature[INDEX_HOTLID2], HOTLID23_TEMP, PWM_FULL);
        }
    }
    else // if ((HotlidTemperature[1]<HOTLID23_TEMP))      //if heater2 is low
    {
        analogWrite(HOTLID2IO, TOPHEATER2PWMHIGH);
        if (btemperatureOut)
        {
            info_displayf("\nTimeMT2\t%.2f\tTopHeater2\tMaintain\tTemperature\t%.2f\tTarget\t%.2f\tPWM\t%d\n", millis() / 1000.0, HotlidTemperature[INDEX_HOTLID2], HOTLID23_TEMP, TOPHEATER2PWMHIGH);
        }
    }

    // process top heater 3
    if (HotlidTemperature[INDEX_HOTLID3] > HOTLID23_TEMP + 10) // if heater3 is too high, in case the PWMLOW is not low enough
    {
        analogWrite(HOTLID3IO, PWM_OFF);
        if (btemperatureOut)
        {
            info_displayf("\nTimeMT3\t%.2f\tTopHeater3\tMaintain\tTemperature\t%.2f\tTarget\t%.2f\tPWM\t%d\n", millis() / 1000.0, HotlidTemperature[INDEX_HOTLID3], HOTLID23_TEMP, PWM_OFF);
        }
    }
    else if (HotlidTemperature[INDEX_HOTLID3] > HOTLID23_TEMP + 5) // if heater3 is at high than 5 degree
    {
        analogWrite(HOTLID3IO, TOPHEATER3PWMLOW);
        if (btemperatureOut)
        {
            info_displayf("\nTimeMT3\t%.2f\tTopHeater3\tMaintain\tTemperature\t%.2f\tTarget\t%.2f\tPWM\t%d\n", millis() / 1000.0, HotlidTemperature[INDEX_HOTLID3], HOTLID23_TEMP, TOPHEATER3PWMLOW);
        }
    }
    else if (HotlidTemperature[INDEX_HOTLID3] < HOTLID23_TEMP - 10) // if heater3 is too low, in case the PWMHIGH is not high enough
    {
        analogWrite(HOTLID3IO, PWM_Heater23);
        if (btemperatureOut)
        {
            info_displayf("\nTimeMT3\t%.2f\tTopHeater3\tMaintain\tTemperature\t%.2f\tTarget\t%.2f\tPWM\t%d\n", millis() / 1000.0, HotlidTemperature[INDEX_HOTLID3], HOTLID23_TEMP, PWM_FULL);
        }
    }
    else // if ((HotlidTemperature[2]<HOTLID23_TEMP))      //if heater3 is low
    {
        analogWrite(HOTLID3IO, TOPHEATER3PWMHIGH);
        if (btemperatureOut)
        {
            info_displayf("\nTimeMT3\t%.2f\tTopHeater3\tMaintain\tTemperature\t%.2f\tTarget\t%.2f\tPWM\t%d\n", millis() / 1000.0, HotlidTemperature[INDEX_HOTLID3], HOTLID23_TEMP, TOPHEATER3PWMHIGH);
        }
    }
}

void PIDControl::MaintainHotlid23()
{
    if (!_topThermometer.getNewTemperatureFlag()) // if new temperature is not ready, then return directly.
    {
        return;
    }
    if (_sensor6035.bSensorReadingGet())
    {
        // Opto sensor reading now, stop heating hotlid, to make sure the power is stable

        // old PCB, V1.2
        analogWrite(HOTLID23IO, PWM_OFF);

        // new PCB, V1.3
        analogWrite(HOTLID2IO, PWM_OFF);
        analogWrite(HOTLID3IO, PWM_OFF);
        if (btemperatureOut)
        {
            if (strcmp(_ForteSetting.parameter.PCB_version, "V1.3") < 0) // if it's old PCB version
            {
                info_displayf("\nTimeMT23\t%.2f\tTopHeater2&3\tMaintain\tTemperature\t%.2f\t%.2f\tTarget\t%.2f\tPWM\t0\n", millis() / 1000.0, HotlidTemperature[INDEX_HOTLID2], HotlidTemperature[INDEX_HOTLID3], HOTLID23_TEMP);
            }
            else
            {
                info_displayf("\nTimeMT2\t%.2f\tTopHeater2\tMaintain\tTemperature\t%.2f\tTarget\t%.2f\tPWM\t0\n", millis() / 1000.0, HotlidTemperature[INDEX_HOTLID2], HOTLID23_TEMP);
                info_displayf("\nTimeMT3\t%.2f\tTopHeater3\tMaintain\tTemperature\t%.2f\tTarget\t%.2f\tPWM\t0\n", millis() / 1000.0, HotlidTemperature[INDEX_HOTLID3], HOTLID23_TEMP);
            }
        }
        _topThermometer.clearNewTemperatureFlag();
        return;
    }
    _topThermometer.clearNewTemperatureFlag();

    if (bhotlid23Simu)
    {
        HotlidTemperature[INDEX_HOTLID2] = HOTLID23_TEMP + 0.5;
        HotlidTemperature[INDEX_HOTLID3] = HOTLID23_TEMP + 0.5;
        stopAllHeating();
        return;
    }

    if (strcmp(_ForteSetting.parameter.PCB_version, "V1.3") < 0) // if it's old PCB version
    {
        maintainOldLid23();
    }
    else
    {
        maintainNewLid23();
    }
}

bool PIDControl::getphase2ready()
{
    return pidStep == epid23ready; // whether heater is finished heating
}

void PIDControl::Maintain2_67()
{
    if (bheater2Simu)
    {
        bottomTemperature[1] = AMPLIF_TEMP + 0.5;
        stopAllHeating();
        return;
    }
    // TARGET_TEMP
    CURRENT_TEMP_PID = bottomTemperature[1]; // temperatureOffset;
    TARGET_TEMP = AMPLIF_TEMP;
    // if(CURRENT_TEMP_PID > TARGET_TEMP+1)
    // {
    //     analogWrite(HEATER2IO, PWM_OFF);
    //     myPID2->Compute();       //PID compute, but don't use the result, to decrease the overheat risk
    //     if (btemperatureOut)
    //     {
    //         info_displayf("\nTimeMB2\t%.2f\tHeater2\tMaintain\tTemperature\t%.4g\tTarget\t%.2f\tPWM\t0\n", millis()/1000.0, CURRENT_TEMP_PID, TARGET_TEMP);
    //     }
    //     return;
    // }
    // else
    if (CURRENT_TEMP_PID > TARGET_TEMP + OVERHEAT_THRESHOLD2)
    {
        stopAllHeating();
        _displayCLD.ErrorProcess("Heater2 over heat", String(CURRENT_TEMP_PID));
        info_displayf("\nTimeMB2\t%.2f\tHeater2\tOverHeat&Rerun\tTemperature\t%.4g\tTarget\t%.2f\tPWM\t0\n", millis() / 1000.0, CURRENT_TEMP_PID, TARGET_TEMP);
        // ESP.restart();      //over heat
        // _ForteSetting.rerun();
        _PIDControl.rerun();
        return;
    }
    else if (CURRENT_TEMP_PID < TARGET_TEMP + UNDERHEAT_THRESHOLD2)
    {
        stopAllHeating();
        _displayCLD.ErrorProcess("Heater2 under heat", String(CURRENT_TEMP_PID));
        info_displayf("\nTimeMB2\t%.2f\tHeater2\tUnderHeat&Rerun\tTemperature\t%.4g\tTarget\t%.2f\tPWM\t0\n", millis() / 1000.0, CURRENT_TEMP_PID, TARGET_TEMP);
        // ESP.restart();      //over heat
        // _ForteSetting.rerun();
        _PIDControl.rerun();
        return;
    }
    myPID2->Compute();
    //// if (RESPONSE_SIGNAL > 120) {RESPONSE_SIGNAL = 120.0;}        //this is used to lower power usage, remove as the power is high enough now
    RESPONSE_SIGNAL = RESPONSE_SIGNAL * 1.0;
    analogWrite(HEATER2IO, (int)RESPONSE_SIGNAL);
    if (btemperatureOut)
    {
        info_displayf("\nTimeMB2\t%.2f\tHeater2\tMaintain\tTemperature\t%.4g\tTarget\t%.2f\tPWM\t%d\n", millis() / 1000.0, CURRENT_TEMP_PID, TARGET_TEMP, (int)RESPONSE_SIGNAL);
    }
}

void PIDControl::StopHeating()
{
    analogWrite(HEATER1IO, PWM_OFF); // switch off heater1
    // analogWrite(HOTLID1IO, PWM_OFF); // switch off hotlid1
    analogWrite(HEATER2IO, PWM_OFF); // switch off heater2
    analogWrite(HEATER3IO, PWM_OFF); // switch off heater3
    // PCB V1.2
    analogWrite(HOTLID23IO, PWM_OFF);
    // PCB V1.3
    analogWrite(HOTLID2IO, PWM_OFF);
    analogWrite(HOTLID3IO, PWM_OFF);
}

// void PIDControl::setepidfinish()
// {
//     pidStep = epidfinish;
// }

// stop all heating when error happen or finish one session
void PIDControl::stopAllHeating()
{
    analogWrite(HEATER1IO, PWM_OFF); // switch off heater1
    // analogWrite(HOTLID1IO, PWM_OFF); // switch off hotlid1
    analogWrite(HEATER2IO, PWM_OFF); // switch off heater2
    analogWrite(HEATER3IO, PWM_OFF); // switch off heater3
    // PCB V1.2
    analogWrite(HOTLID23IO, PWM_OFF);
    // PCB V1.3
    analogWrite(HOTLID2IO, PWM_OFF);
    analogWrite(HOTLID3IO, PWM_OFF);
}

void PIDControl::stopHeaterBottom(void)
{
    analogWrite(HEATER1IO, PWM_OFF); // switch off heater1
    analogWrite(HEATER2IO, PWM_OFF); // switch off heater2
    analogWrite(HEATER3IO, PWM_OFF); // switch off heater3
}

void PIDControl::stopHeaterTop(void)
{
    // PCB V1.2
    analogWrite(HOTLID23IO, PWM_OFF);
    // PCB V1.3
    analogWrite(HOTLID2IO, PWM_OFF);
    analogWrite(HOTLID3IO, PWM_OFF);
}

PIDControl _PIDControl;
