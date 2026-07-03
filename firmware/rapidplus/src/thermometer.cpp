#include "thermometer.h"

/***********************************************************************
 * Function: thermometer()
 * Description: Constructor for the thermometer class. Creates a OneWire bus
 *  instance on the supplied pin and wraps it in a new DallasTemperature
 *  driver stored in DallasSensor.
 * pramameter: Wire - the GPIO pin number used for the OneWire bus.
 *  return: none
 */
thermometer::thermometer(int Wire)
{
    // OneWire OneWireSensor(ONE_WIRE);
    OneWireSensor = new OneWire(Wire);
    this->DallasSensor = new DallasTemperature(OneWireSensor);
}

/***********************************************************************
 * Function: ~thermometer()
 * Description: Destructor for the thermometer class. Performs no cleanup
 *  (the DallasSensor deletion is commented out).
 * pramameter: none
 *  return: none
 */
thermometer::~thermometer()
{
    // delete this->DallasSensor;
}

/***********************************************************************
 * Function: begin()
 * Description: Initializes the Dallas temperature sensor. Starts the driver,
 *  switches it to non-blocking conversion mode, triggers the first
 *  temperature conversion request and waits 1 second.
 * pramameter: none
 *  return: none
 */
void thermometer::begin()
{
    // Start up the temperature sensor and PID
    this->DallasSensor->begin();
    this->DallasSensor->setWaitForConversion(false); // here configure to non-block mode

    this->DallasSensor->requestTemperatures();
    delay(1000);
}

/***********************************************************************
 * Function: loop()
 * Description: Periodic update entry point; delegates to
 *  readHeatBlkTemperature() to poll for completed conversions.
 * pramameter: none
 *  return: none
 */
void thermometer::loop()
{
    readHeatBlkTemperature();
}

/***********************************************************************
 * Function: readHeatBlkTemperature()
 * Description: If a temperature conversion has completed, gets the number of
 *  detected sensors, reads each sensor's Celsius temperature into sensorTemp[],
 *  then requests a new conversion and sets the newTemeraturePID and
 *  newTemperatureScreen flags.
 * pramameter: none
 *  return: none
 */
void thermometer::readHeatBlkTemperature()
{
    if (this->DallasSensor->isConversionComplete())
    {
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
/***********************************************************************
 * Function: getTempSensorQuantity()
 * Description: Returns the number of temperature sensors detected on the
 *  OneWire bus during the last reading.
 * pramameter: none
 *  return: uint8_t - the tempSensorQuantity member value.
 */
uint8_t thermometer::getTempSensorQuantity()
{
    return tempSensorQuantity;
}

/***********************************************************************
 * Function: getTemperature()
 * Description: Returns a pointer to the internal sensorTemp array holding
 *  the most recent Celsius readings for all detected sensors.
 * pramameter: none
 *  return: double* - pointer to the sensorTemp temperature array.
 */
double *thermometer::getTemperature()
{
    return sensorTemp;
}

/***********************************************************************
 * Function: getNewTemperatureFlag()
 * Description: Returns the newTemeraturePID flag indicating whether a fresh
 *  temperature reading is available for the PID controller.
 * pramameter: none
 *  return: bool - the newTemeraturePID flag value.
 */
bool thermometer::getNewTemperatureFlag()
{
    return newTemeraturePID;
}

/***********************************************************************
 * Function: clearNewTemperatureFlag()
 * Description: Clears the newTemeraturePID flag, marking that the latest
 *  temperature reading has been consumed by the PID controller.
 * pramameter: none
 *  return: none
 */
void thermometer::clearNewTemperatureFlag()
{
    newTemeraturePID = false; // used to configure PID already
}

/***********************************************************************
 * Function: getNewTemperatureScreenFlag()
 * Description: Returns the newTemperatureScreen flag indicating whether a
 *  fresh temperature reading is available for the display/screen.
 * pramameter: none
 *  return: bool - the newTemperatureScreen flag value.
 */
bool thermometer::getNewTemperatureScreenFlag()
{
    return newTemperatureScreen;
}

/***********************************************************************
 * Function: clearNewTemperatureScreenFlag()
 * Description: Clears the newTemperatureScreen flag, marking that the latest
 *  temperature reading has been consumed by the display/screen.
 * pramameter: none
 *  return: none
 */
void thermometer::clearNewTemperatureScreenFlag()
{
    newTemperatureScreen = false;
}

thermometer _bottomThermometer(ONE_WIRE); // this is the 3 sensors used for heating block
thermometer _topThermometer(ONE_WIRE1);   // this is the 4 sensors used for hot lid and ambient temperature
