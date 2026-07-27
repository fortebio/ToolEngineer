#include "define.h"
#include <vector>
#include <ArduinoJson.h>
#include <cstring>

typedef enum
{
    /*************************************
     * 0: Sensor Heater
     * 1: Sensor Light */
    errorHeaterSensor,
    errorLightSensor,
} errorModule_t;

typedef enum
{
    /*************************************
     * Sensor Amplification
     *   0: no data from sensor
     *   1: not all data from sensors
     *   2: wrong data from sensor
     *   3: Sensor too dark
     *   4: Sensor too bright */
    errorNoData,
    errorWrongData,
    errorTooDark,
    errorTooBright
} errorSensorLight_t;

typedef enum
{
    /*************************************
     * Sensor Heater
     *   0: Overheat
     *   1: Underheat
     *   2: Heater disconnected
     *   3: Wrong data from sensor */
    errorOverheat,
    errorUnderheat,
    errorHeaterDisconnected,
    errorHeaterWrongData
} errorSensorHeater_t;

static const char *errorModuleStr[] = {
    "Sensor Heater",
    "Sensor Light",
};

static const char *errorTypeAmplificationStr[] = {
    "No data from sensor",
    "Wrong data from sensor",
    "Sensor too dark",
    "Sensor too bright"};
static const char *errorTypeHeaterStr[] = {
    "Overheat",
    "Underheat",
    "Heater disconnected",
    "Wrong data from sensor"};

static const char *errorProcessSensorLight[] = {
    "In Process Step Sensor Wait",
    "In Process Step Sensor Preheat",
    "In Process Step Sensor Maintain",
    "In Process Step Sensor Start",
    "In Process Amplification 40 min",
    "In Process Step Sensor Calibration",
};
static const char *errorProcessSensorHeater[] = {
    "In Device start up",
    "In Process Start Heat Lysis",
    "In Process Preheat Lysis",
    "In Process Lysis 10 min",
    "In Process Lysis 10 min Finish",
    "In Process Step Heat Amplipication Heater 1",
    "In Process Step Preheat Amplipication Heater 1",
    "In Process Step Heat Amplipication Heater 2",
    "In Process Step Preheat Amplipication Heater 2",
    "In Process Step Heat Amplipication Hotlid 1,2",
    "In Process Step heat Amplification 40 min",
};

static const char *errorSlotSensorLightStr[] = {
    "Slot 1",
    "Slot 2",
    "Slot 3",
    "Slot 4",
    "Slot 5",
    "Slot 6",
    "Slot 7",
    "Slot 8",
    "Slot 9",
    "Slot 10"};
static const char *errorSlotSensorHeaterStr[] = {
    "Heater Lysis",
    "Heater Amplification 1",
    "Heater Amplification 2",
    "Heater Hotlid 1",
    "Heater Hotlid 2"};

static const char **errorProcessStr[] = {
    errorProcessSensorHeater,
    errorProcessSensorLight,
};
static const char **errorTypeStr[] = {
    errorTypeHeaterStr,
    errorTypeAmplificationStr,
};
static const char **errorSlotStr[] = {
    errorSlotSensorHeaterStr,
    errorSlotSensorLightStr};

typedef struct
{
    uint8_t errorModule = 0;      /*************************************
                                   * 0: Sensor Light
                                   * 1: Sensor Heater */
    uint8_t errorType = 0;        /*************************************
                                   * Sensor Amplification
                                   *   0: No error
                                   *   1: no data from sensor
                                   *   2: not all data from sensors
                                   *   3: wrong data from sensor
                                   *   4: Sensor too dark
                                   *   5: Sensor too bright
                                   * Sensor Heater
                                   *   0: No error
                                   *   1: Overheat
                                   *   2: Underheat
                                   *   3: Heater disconnected
                                   *   4: Wrong data from sensor */
    uint8_t errorProcessStep = 0; /* record which step of the error process, used for error process. For example, if the error is "no data from sensor", then we can do different process for different step, such as first time, second time, third time, etc. The value is starting from 0, and increase by 1 each time the same error happened. */
    uint8_t errorSlot = 0;
} ErrorRecord_t;

typedef class
{
public:
    std::vector<ErrorRecord_t> error;
    uint8_t numUnit = 0; // quantity of the unit that has error, used for error process. For example, if the error is "no data from sensor", then we can check how many sensors have no data, and do different process for different quantity, such as 1 sensor, 2 sensors, 3 sensors, etc.

    void addError(uint8_t errorModule, uint8_t errorType, uint8_t errorProcessStep, uint8_t errorSlot)
    {
        if (searchError(errorModule, errorType, errorProcessStep, errorSlot) != 255 ||
            (numUnit >= 30))
        {
            // Serial.println("The same error record already exists, not adding again!");
            return;
        }

        ErrorRecord_t _errorRecord;
        _errorRecord.errorModule = errorModule;
        _errorRecord.errorType = errorType;
        _errorRecord.errorProcessStep = errorProcessStep;
        _errorRecord.errorSlot = errorSlot;
        numUnit++;
        error.push_back(_errorRecord);
    }
    void saveErrorToEEPROM()
    {
        // This runs from ~17 PID safety paths on ControlTask at any time - it is the
        // concurrent "attacker" that corrupts a /reviewlast EEPROM read. Serialize it.
        eepromLock();
        EEPROM.begin(_EEPROM_SIZE);
        EEPROM.put(ADDR_ERROR_NUMBER_UNIT, numUnit); // save the quantity of the unit that has error to EEPROM, used for error process)
        EEPROM.put(ADDR_ERROR_FLAG, 1);              // set the error flag to 1, which means there is error in the device, used for error process
        for (size_t i = 0; i < numUnit; i++)
        {
            EEPROM.put(ADDR_ERROR_RECORD + i * sizeof(ErrorRecord_t), error[i]);
        }
        EEPROM.commit();
        EEPROM.end();
        eepromUnlock();
    }
    void readErrorFromEEPROM()
    {
        eepromLock();
        EEPROM.begin(_EEPROM_SIZE);
        EEPROM.get(ADDR_ERROR_NUMBER_UNIT, numUnit); // read the quantity of the unit that has error from EEPROM, used for error process)
        uint8_t errorFlag;
        EEPROM.get(ADDR_ERROR_FLAG, errorFlag); // read the error flag from EEPROM, used for error process
        if (errorFlag == 1)
        {
            error.clear();
            for (size_t i = 0; i < numUnit; i++)
            {
                ErrorRecord_t _errorRecord;
                EEPROM.get(ADDR_ERROR_RECORD + i * sizeof(ErrorRecord_t), _errorRecord);
                error.push_back(_errorRecord);
            }
        }
        else
        {
            // if the error flag is not 1, which means there is no valid error record in the EEPROM, so we need to clear the error record in the device, used for error process
            clear();
        }
        EEPROM.end();
        eepromUnlock();
    }
    String decodeError(ErrorRecord_t _errorRecord)
    {
        char errorMsg[256];
        sprintf(errorMsg, "[%s]- %s %s ",
                errorModuleStr[_errorRecord.errorModule],
                errorTypeStr[_errorRecord.errorModule][_errorRecord.errorType],
                errorProcessStr[_errorRecord.errorModule][_errorRecord.errorProcessStep]);
        return String(errorMsg);
    }
    unsigned short EncodeError(ErrorRecord_t _errorRecord)
    {
        return _errorRecord.errorModule * 1000 +
               _errorRecord.errorType * 100 +
               _errorRecord.errorProcessStep * 10 +
               _errorRecord.errorSlot;
    }
    void clear()
    {
        error.clear();
        numUnit = 0;
    }

    void clearEEPROM()
    {
        eepromLock();
        EEPROM.begin(_EEPROM_SIZE);
        uint8_t clearFlag = 0;
        EEPROM.put(ADDR_ERROR_FLAG, clearFlag); // clear the error flag to 0, which means there is no error in the device, used for error process
        EEPROM.put(ADDR_ERROR_NUMBER_UNIT, 0);  // clear the quantity of the unit that has error to 0, which means there is no error in the device, used for error process
        EEPROM.commit();
        EEPROM.end();
        eepromUnlock();
    }
    uint8_t searchError(uint8_t errorModule, uint8_t errorType, uint8_t errorProcessStep, uint8_t errorSlot)
    {
        for (size_t i = 0; i < error.size(); i++)
        {
            if (error[i].errorModule == errorModule &&
                error[i].errorType == errorType &&
                error[i].errorProcessStep == errorProcessStep &&
                error[i].errorSlot == errorSlot)
            {
                return i;
            }
        }
        return 255; // not found the error record
    }
    void printAllError()
    {
        for (size_t i = 0; i < numUnit; i++)
        {
            Serial.printf("Slot: %s\n", errorSlotStr[error[i].errorModule][error[i].errorSlot]);
            Serial.printf("Error: %04d\n", EncodeError(error[i]));
            Serial.println(decodeError(error[i]));
        }
    }

} ErrorCheck;

void postError_fullGoogleSheet(void);
void postError_Googlesheet(uint8_t errorModule, uint8_t errorType, uint8_t errorProcessStep, uint8_t errorSlot);

extern ErrorCheck error;