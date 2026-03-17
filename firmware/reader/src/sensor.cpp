#include "sensor.h"
#include "tcs.h"
#include <EEPROM.h>

/* POSITIVE THRESHOLD */
uint16_t Threshold_PO;
uint16_t Threshold_NE;
uint16_t Threshold_EHP;
uint16_t Threshold_EMS;
uint16_t Threshold_WSSV;

sensor::sensor(/* args */)
{
    pinMode(ON_LED, OUTPUT);
    digitalWrite(ON_LED, LOW);
}

sensor::~sensor()
{
}

void sensor::begin()
{
    this->clear();
    uint8_t checkThreshold = 0;
    uint8_t checkInitializeSample = 0;
    uint8_t checkCalib = 0;

    EEPROM.begin(_EEPROM_SIZE);

    EEPROM.get(ADDR_LANGUAGE, _displayLCD.language);
    if (_displayLCD.language != VietNamese &&
        _displayLCD.language != English &&
        _displayLCD.language != Taiwanese &&
        _displayLCD.language != Chinese)
    {
        _displayLCD.language = English;
        EEPROM.put(ADDR_LANGUAGE, _displayLCD.language);
        EEPROM.commit();
    }

    EEPROM.get(ADDR_CHECK_CALIB, checkCalib);
    if (checkCalib != 0x01)
    {
        this->valueCalibMax = 3000;
        this->valueCalibMin = 0;
    }
    else
    {
        EEPROM.get(ADDR_VALUE_CALIB_MAX, this->valueCalibMax);
        EEPROM.get(ADDR_VALUE_CALIB_MIN, this->valueCalibMin);
    }

    EEPROM.end();

    delay(50);

    Wire.begin(32, 33);
    rgb_sensor.begin();
}

void sensor::read_Sensor(uint8_t num)
{
    for (int i = 0; i < numSample; i++)
    {
        this->Data_Sensor1[i] = analogRead(num);
        // this->Data_Sensor1[i] = analogReadMilliVolts(sensor)
    }
}

bool sensor::read_Allsensor()
{
    digitalWrite(ON_LED, HIGH);
    // rgb_sensor.getData();
    // delay(1000);
    if (rgb_sensor.detectI2C())
    {
        rgb_sensor.getData();
        delay(1000);
        dbg_sensor("TCS34725 detected");
        rgb_sensor.getData();
        // rgb_sensor.getData();
        for (int i = 0; i < numSample; i++)
        {
            rgb_sensor.getData();
            this->Data_Sensor1[i] = rgb_sensor.lux;
            delay(50);
        }
        return true;
    }
    else
    {
        delay(1000);
        for (int i = 0; i < numSample; i++)
        {
            //     // rgb_sensor.getData();
            this->Data_Sensor1[i] = 0;
            delay(50);
        }
        dbg_sensor("TCS34725 not detected");
        return false;
    }
}

uint16_t sensor::calib_SensorLight()
{
    digitalWrite(ON_LED, HIGH);
    delay(620);

    float sumCalib = 0;
    rgb_sensor.getData();
    rgb_sensor.getData();

    for (int i = 0; i < 5; i++)
    {
        rgb_sensor.getData();
        dbg_sensor("sensor_calib %d: %f", i, rgb_sensor.lux);
        sumCalib += rgb_sensor.lux;
    }
    digitalWrite(ON_LED, LOW);

    float valueCalib = (float)(sumCalib / 5.0) * 1000.0;
    dbg_sensor("TB sensor_calib: %f", valueCalib);
    return (uint16_t)valueCalib;
}

void sensor::format_CalibSensor()
{
    for (int i = 0; i < numSensor; i++)
    {
        EEPROM.put(i * 2, (int16_t)0);
    }
    EEPROM.commit();
}

void sensor::handle_Sensor()
{
    this->sum_Sensor[0] = 0;
    for (int k = 0; k < numSample; k++)
    {
        // dbg_sensor("sensor %f", this->Data_Sensor1[k]);
        this->sum_Sensor[0] += (float)this->Data_Sensor1[k];
        // dbg_sensor("sum sensor%d: %f", k, this->sum_Sensor[0]);
    }

    uint32_t value_sensor = round((float)(sum_Sensor[0] / (float)((numSample) * 1.0)) * 1000.0);
    this->raw_Sensor[_displayLCD.couter - 1] = value_sensor;

    if (value_sensor < this->valueCalibMin)
        this->result_Sensor[0][_displayLCD.couter - 1] = valueMinsensor;
    else if (value_sensor > this->valueCalibMax)
        this->result_Sensor[0][_displayLCD.couter - 1] = valueMAXsensor;
    else
        this->result_Sensor[0][_displayLCD.couter - 1] = map(value_sensor, this->valueCalibMin, this->valueCalibMax, valueMinsensor, valueMAXsensor);

    dbg_sensor("sensor_value: %d", value_sensor);
    dbg_sensor("sensor: %d", this->result_Sensor[0][_displayLCD.couter - 1]);
}

void sensor::Average_Result()
{
    this->AverageResult[0] = 0;
    // for (int i = 0; i < numSensor; i++)
    // {
    for (int i = 0; i < 3; i++)
    {
        this->AverageResult[0] += result_Sensor[0][i];
    }
    // }

    AverageResult[0] = AverageResult[0] / 3;
}

void sensor::clear()
{
    for (int i = 0; i < numSensor; i++)
    {
        sum_Sensor[i] = 0;
        AverageResult[i] = 0;
        sensor_kalman[i] = 0;
        result_Sensor[i][0] = 0;
        result_Sensor[i][1] = 0;
        result_Sensor[i][2] = 0;
        sensorError[0] = {true};
        sensorError[1] = {true};
        sensorError[2] = {true};
    }
}
void sensor::loop()
{
    if (flagReadSensor)
    {
        if (_displayLCD.couter < 3)
        {
            _displayLCD.couter++;
            this->sensorError[_displayLCD.couter - 1] = this->read_Allsensor();
            this->handle_Sensor();
        }
        else if (_displayLCD.couter >= 3)
        {
            _displayLCD.type_infor = escreenAverageResult;
            _displayLCD.changeScreen = true;
            flagReadSensor = false;
            digitalWrite(ON_LED, LOW);
        }
    }

    else if (flagCalibSensor)
    {
        flagCalibSensor = false;

        if (this->typecalib == 0)
        {
            _displayLCD.waiting_Calib();
            this->valueCalibMax = this->calib_SensorLight();
            this->typecalib = 1;
            _displayLCD.type_infor = ecalibSensor;
            _displayLCD.changeScreen = true;
        }
        else if (this->typecalib == 1)
        {
            _displayLCD.waiting_Calib();
            this->valueCalibMin = this->calib_SensorLight();
            _sensor.typecalib = 0;
            EEPROM.begin(_EEPROM_SIZE);
            dbg_sensor("calib_Min: %d", this->valueCalibMin);
            dbg_sensor("calib_Max: %d", this->valueCalibMax);
            EEPROM.put(ADDR_CHECK_CALIB, 0x01);
            EEPROM.put(ADDR_VALUE_CALIB_MAX, this->valueCalibMax);
            EEPROM.put(ADDR_VALUE_CALIB_MIN, this->valueCalibMin);
            EEPROM.commit();
            EEPROM.end();
            _displayLCD.screen_Calib_Complete();
            ESP.restart();
        }
        else
        {
            this->typecalib = 0;
        }
    }

    else if (flagformatCalib)
    {
        this->format_CalibSensor();
        flagformatCalib = false;
        _displayLCD.type_infor = escreenStart;
        _displayLCD.changeScreen = true;
    }
}

sensor _sensor;
