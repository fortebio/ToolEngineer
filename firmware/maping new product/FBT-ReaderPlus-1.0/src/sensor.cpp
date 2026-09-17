#include "sensor.h"
#include "tcs.h"

/* POSITIVE THRESHOLD */
uint16_t Threshold_PO;
uint16_t Threshold_NE;
uint16_t Threshold_EHP;
uint16_t Threshold_EMS;
uint16_t Threshold_WSSV;

sensor::sensor()
{
    ledcSetup(PWM_channel, 5000, 8);
    ledcAttachPin(PWM, PWM_channel);
    ledcWrite(PWM_channel, 0);
    for (uint8_t i = 0; i < numSensor; i++)
    {
        pinMode(ledPins[i], OUTPUT);
        digitalWrite(ledPins[i], LOW); 
    }
}

sensor::~sensor()
{
	/*  */
}

void sensor::LED_on(uint8_t channel)
{
    ledcWrite(PWM_channel, PWM_LED[channel]);
    digitalWrite(ledPins[channel], HIGH);
}

void sensor::LED_off(uint8_t channel)
{
    digitalWrite(ledPins[channel], LOW);
    ledcWrite(PWM_channel, 0);
    delay(10);
}

void sensor::LED_off_All()
{
    for (size_t i = 0; i < numSensor; i++)
    {
        digitalWrite(ledPins[i], LOW);
    }
    ledcWrite(PWM_channel, 0);  
}

void sensor::begin()
{
	this->clear();
    uint8_t checkThreshold = 0;

	EEPROM.begin(_EEPROM_SIZE);
    for (uint8_t i = 0; i < numSensor; i++)
    {
        EEPROM.get(ADDR_VALUE_CALIB_MAX(i), this->valueCalibMax[i]);
	    EEPROM.get(ADDR_VALUE_CALIB_MIN(i), this->valueCalibMin[i]);
    }
	EEPROM.get(ADDR_CHECK_THRESHOLD, checkThreshold);
    if (checkThreshold != 0)
    {
        for (uint8_t addr_threshold = PC; addr_threshold < SICK_NUMBER; addr_threshold++)
        {
            this->valueThreshold[addr_threshold] = 600;
            EEPROM.put(ADDR_THRESHOLD_POSITIVE(addr_threshold), 600);
            EEPROM.commit();
        }
        EEPROM.put(ADDR_CHECK_THRESHOLD, 0);
        EEPROM.commit();
    }
    else
    {
        for (uint8_t addr_threshold = PC; addr_threshold < SICK_NUMBER; addr_threshold++)
        {
            EEPROM.get(ADDR_THRESHOLD_POSITIVE(addr_threshold), this->valueThreshold[addr_threshold]);
        }
    }

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
    EEPROM.end();
	
	delay(50);
	
	I2CMux.begin();
    for (uint8_t iChannel = 0; iChannel < numSensor; iChannel++)
    {
        I2CMux.selectChannel(I2C_Channel[iChannel]);
        rgb_sensor.begin();
    }
}

/* READ SENSOR */
void sensor::read_Sensor(uint8_t channel)
{
    this->LED_on(channel);
    delay(1000);

    I2CMux.selectChannel(I2C_Channel[channel]);
    for (int i = 0; i < numSample; i++)
    {
        rgb_sensor.getData();
        this->Data_Sensor[channel][i] = rgb_sensor.lux;
    }
}

void sensor::read_All_Sensor()
{
	for (uint8_t iChannel = 0; iChannel < numSensor; iChannel++)
	{
        this->LED_on(iChannel);
        delay(1000);

		I2CMux.selectChannel(I2C_Channel[iChannel]);
		for (int i = 0; i < numSample; i++)
		{
			rgb_sensor.getData();
			this->Data_Sensor[iChannel][i] = rgb_sensor.lux;
		}
	}
}

/* HANDLE SENSOR */
void sensor::handle_Sensor(uint8_t channel)
{
    this->sum_Sensor[channel] = 0;
    for (int i = 0; i < numSample; i++)
    {
        this->sum_Sensor[channel] += (float)this->Data_Sensor[channel][i];
    }
    value_sensor[channel] = round((float)(sum_Sensor[channel] / (float)((numSample) * 1.0)) * 1000.0);
    
    if (value_sensor[channel] < this->valueCalibMin[channel])
        this->result_Sensor[channel][_displayLCD.counter - 1] = valueMinsensor;
    else if (value_sensor[channel] > this->valueCalibMax[channel])
        this->result_Sensor[channel][_displayLCD.counter - 1] = valueMAXsensor;
    else
        this->result_Sensor[channel][_displayLCD.counter - 1] = map(value_sensor[channel], this->valueCalibMin[channel], this->valueCalibMax[channel], valueMinsensor, valueMAXsensor);
}

void sensor::handle_All_Sensor()
{
	for (uint8_t i = 0; i < numSensor; i++)
	{
		this->sum_Sensor[i] = 0;
		for (uint8_t j = 0; j < numSample; j++)
		{
			this->sum_Sensor[i] += (float)this->Data_Sensor[i][j];
		}
        value_sensor[i] = round((float)(sum_Sensor[i] / (float)((numSample) * 1.0)) * 1000.0);
        
        if (value_sensor[i] < this->valueCalibMin[i])
            this->result_Sensor[i][_displayLCD.counter - 1] = valueMinsensor;
        else if (value_sensor[i] > this->valueCalibMax[i])
            this->result_Sensor[i][_displayLCD.counter - 1] = valueMAXsensor;
        else
            this->result_Sensor[i][_displayLCD.counter - 1] = map(value_sensor[i], this->valueCalibMin[i], this->valueCalibMax[i], valueMinsensor, valueMAXsensor);
    }
}

/* AverageResult */
void sensor::Average_Result(uint8_t channel)
{
	this->AverageResult[channel] = 0;
    for (uint i = 0; i < numSample; i++)
    {
        this->AverageResult[channel] += result_Sensor[channel][i];
    }
    AverageResult[channel] = AverageResult[channel] / numSample;
}

void sensor::Average_All_Result()
{
    for (uint8_t i = 0; i < numSensor; i++)
	{	
        this->AverageResult[i] = 0;
		for (uint8_t j = 0; j < numSample; j++)
		{
			this->AverageResult[i] += result_Sensor[i][j];
		}
		AverageResult[i] = AverageResult[i] / numSample;
	}
}

/* CLEAR */
void sensor::clear()
{
	for (uint8_t i = 0; i < numSensor; i++)
	{
		sum_Sensor[i] = 0;
        AverageResult[i] = 0;
        //sensor_kalman[i] = 0;
        result_Sensor[i][0] = 0;
        result_Sensor[i][1] = 0;
        result_Sensor[i][2] = 0;
	}
}

/* Calib */
uint16_t sensor::calib_Sensor(uint8_t channel)
{
    this->LED_on(channel);
    delay(620);

    float sumCalib = 0;
    
    I2CMux.selectChannel(I2C_Channel[channel]);
    for (uint8_t i = 0; i < 5; i++)
    {
        rgb_sensor.getData();
        sumCalib += rgb_sensor.lux;
    }
    this->LED_off(channel);

    float valueCalib = (float)(sumCalib / 5.0) * 1000.0;
    return (uint16_t)valueCalib;
}

void sensor::format_CalibSensor(uint8_t address)
{
    for (uint8_t i = 0; i < 1; i++)
    {
        EEPROM.put((address * 2), (int16_t)0);  
		EEPROM.put((address * 2) + ADDR_OFFSET, (int16_t)0);
    }
    EEPROM.commit();
}

void sensor::format_All_CalibSensor()
{
    for (uint8_t i = 0; i < 4; i++)
    {
        EEPROM.put((i * 2), (uint16_t)0);
        EEPROM.put((i * 2) + ADDR_OFFSET, (uint16_t)0);
    }
    EEPROM.commit(); 
}

void sensor::loop()
{
    if (flagReadSensor)
    {
        /*
        switch (slot)
        {
        case 0:
        case 1:
        case 2:
        case 3:
            if (_displayLCD.counter < numSampling)
            {
                _displayLCD.counter++;
                this->read_Sensor(slot);
                this->handle_Sensor(slot);
            }
            else if (_displayLCD.counter >= numSampling)
            {
                _displayLCD.type_infor = escreenAverageResult;
                _displayLCD.changeScreen = true;
                flagReadSensor = false;
                this->LED_off(slot);
            }
            break;
        case 4:
        */
            if (_displayLCD.counter < numSampling)
            {
                _displayLCD.counter++;
                this->read_All_Sensor();
                this->handle_All_Sensor();
            }
            else if (_displayLCD.counter >= numSampling)
            {
                _displayLCD.type_infor = escreenAverageResult;
                _displayLCD.changeScreen = true;
                flagReadSensor = false;
                this->LED_off_All();
            }
            //break;
        //default:
           //break;
        //}
    }
    else if (flagCalibSensor)
    {
        flagCalibSensor = false;

        if (this->typecalib == 0)
        {
            _displayLCD.waiting_Calib();
            this->valueCalibMax[slot] = this->calib_Sensor(slot);
            this->typecalib = 1;
            _displayLCD.type_infor = ecalibSensor;
            _displayLCD.changeScreen = true;
        }
        else if (this->typecalib == 1)
        {
            _displayLCD.waiting_Calib();
            this->valueCalibMin[slot] = this->calib_Sensor(slot);

            EEPROM.begin(_EEPROM_SIZE);
            EEPROM.put(ADDR_VALUE_CALIB_MAX(slot), this->valueCalibMax[slot]);
            EEPROM.put(ADDR_VALUE_CALIB_MIN(slot), this->valueCalibMin[slot]);
            EEPROM.commit();
            EEPROM.end();

            this->typecalib = 0;
            _sensor.counter_calib += 1;
            _sensor.slot += 1;
            if (_sensor.counter_calib > 3)
            {
                _displayLCD.screen_Calib_Complete();
                ESP.restart();
            }
            else
            {
                _displayLCD.type_infor = ecalibSensor;
                _displayLCD.changeScreen = true;
            }
        }
        else
        {
            this->typecalib = 0;
        }
    }    
    else if (flagformatCalib)
    {
        this->format_All_CalibSensor();
        flagformatCalib = false;
        _displayLCD.type_infor = escreenStart;
        _displayLCD.changeScreen = true;
    }
}

sensor _sensor;