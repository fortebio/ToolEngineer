#ifndef _FORTESETTING_H
#define _FORTESETTING_H

#include "HardwareSerial.h"
#include "buzzer.h"
#include "Fan.h"
#include "PIDControl.h"
#include "define.h"
#include <ArduinoJson.h>
#include "sensor6035.h"
#include "LED.h"


class ForteSetting
{
private:
    /* data */
    char recvData[2*1024];         //received data will store here
    uint32_t recvLen = 0;        //length of the received data
    unsigned long recvTime = 0; //time to receive the last daqta, used to check all data is received
    bool moreMsg = false;       //used for long msg receiving


    bool BuzzerConfig();
    bool FanConfig();
    bool HeaterSimuConfig();
    bool TemperatureOutput();
    bool HeaterStepSet();
    bool JsonDataConfig();
    bool ParaRead();
    bool EEPROMRead();
    bool resultOutput();
    bool restart();
    bool start_amplification_simulation();

    int paraIntSplit(char * source, int *para);

public:
    ForteSetting(/* args */);
    ~ForteSetting();
    
    void begin();
    void loop();
    void rerun();
	
    parastructure parameter;
};

extern ForteSetting _ForteSetting;


#define protoID       _ForteSetting.parameter.device_id
#define OpticalUnits  _ForteSetting.parameter.units

#endif
