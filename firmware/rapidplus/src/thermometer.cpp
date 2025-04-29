#include "thermometer.h"

thermometer::thermometer(int Wire)
{
    // OneWire OneWireSensor(ONE_WIRE);
    OneWireSensor = new OneWire(Wire);
    this->DallasSensor = new DallasTemperature(OneWireSensor);
}

thermometer::~thermometer()
{
    // delete this->DallasSensor;
}

void thermometer::begin()
{
    // Start up the temperature sensor and PID
    this->DallasSensor->begin();
    this->DallasSensor->setWaitForConversion(false); // here configure to non-block mode

    // record start time, RFU to check the timeout issue
    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);
    time_us_start = (int64_t)tv_now.tv_sec * 1000000L + (int64_t)tv_now.tv_usec;
    this->DallasSensor->requestTemperatures();
    delay(1000);
}

void thermometer::loop()
{
    readHeatBlkTemperature();
}

void thermometer::readHeatBlkTemperature()
{
    if (this->DallasSensor->isConversionComplete())
    {
        temperatureReadingTime = millis(); // record the time immediately to try record the time that close to the tempeature reading
        tempSensorQuantity = this->DallasSensor->getDeviceCount();

        for (size_t i = 0; i < tempSensorQuantity; i++)
        {
            sensorTemp[i] = this->DallasSensor->getTempCByIndex(i);
            delay(10);
        }

        this->DallasSensor->requestTemperatures();
        newTemeraturePID = true;
        newTemperatureScreen = true;
    }
}

/* Sensor Quantity */
uint8_t thermometer::getTempSensorQuantity()
{
    return tempSensorQuantity;
}

double *thermometer::getTemperature()
{
    return sensorTemp;
}

bool thermometer::getNewTemperatureFlag()
{
    return newTemeraturePID;
}

void thermometer::clearNewTemperatureFlag()
{
    newTemeraturePID = false; // used to configure PID already
}

bool thermometer::getNewTemperatureScreenFlag()
{
    return newTemperatureScreen;
}

void thermometer::clearNewTemperatureScreenFlag()
{
    newTemperatureScreen = false;
}

// this is the time to read the temperature of all the 3 heater blocks
unsigned long thermometer::getTemperatureReadingTime()
{
    return temperatureReadingTime;
}

thermometer _bottomThermometer(ONE_WIRE); // this is the 3 sensors used for heating block
thermometer _topThermometer(ONE_WIRE1);   // this is the 4 sensors used for hot lid and ambient temperature
