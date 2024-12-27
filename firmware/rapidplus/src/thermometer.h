#ifndef _THERMOMETER_H
#define _THERMOMETER_H

#include <OneWire.h>
#include <DallasTemperature.h>
#include "define.h"


class thermometer
{
private:
    /* data */
    OneWire *OneWireSensor;
    DallasTemperature *DallasSensor;
    int64_t time_us_start;
    uint8_t tempSensorQuantity;
    double sensorTemp[HOTLIDQUANTITY];    //used by bottom heater and top heater
    bool newTemperatureScreen = false;      //flag for screen to display
    bool newTemeraturePID = false;          //flag for PID to control
    unsigned long temperatureReadingTime = 0;       //record the time when read the temperature

public:
    thermometer(int Wire);
    ~thermometer();
    void begin();
    void loop();
    void readHeatBlkTemperature();
    uint8_t getTempSensorQuantity();
    double * getTemperature();
    bool getNewTemperatureFlag();
    void clearNewTemperatureFlag();
    bool getNewTemperatureScreenFlag();
    void clearNewTemperatureScreenFlag();
    unsigned long getTemperatureReadingTime();
};



extern thermometer _bottomThermometer;
extern thermometer _topThermometer;
#endif

