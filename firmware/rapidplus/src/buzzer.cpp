/*
Support on and off
Support loop detection
Support auto configuration, the frequency and duration

*/
#include "buzzer.h"
#include "LED.h"



buzzer::buzzer(/* args */)
{
    // pinMode(BUZZER, OUTPUT);
    // digitalWrite(BUZZER, 1);
}

buzzer::~buzzer()
{
}

void buzzer::loop()
{
    if(bStop)
    {
        BuzzerEnd();
        bStop = false;
        return;
    }
    if (startTime)      //if the buzzer has started
    {
        unsigned long duration = millis() - startTime;
        int oneCycle = (duration_on+duration_off)*times + longInterval;     //this is the duration of one session
        if (duration/oneCycle >= longTimes)         //if the time finished, then end
        {
            BuzzerEnd();
            return;
        }
        unsigned long durationSession = duration%oneCycle;      //the duration in one long cycle
        if (durationSession > oneCycle - longInterval)           //if it's under long interval
        {
            BuzzerOff();
            return;
        }
        if (durationSession%(duration_on+duration_off)<duration_on)     //at the time of buzzer on
        {
            BuzzerOn();
            return;
        }
    }
    BuzzerOff();        //off by default, in case some error happened
}

void buzzer::BuzzerStart()
{
    if(duration_on > 10000)
    {
        info_displayln("The duration of pressing button is too long");
        return;
    }
    if(times > 30)
    {
        info_displayln("repeate time is too many");
        return;
    }

    startTime = millis();
    BuzzerOn();
}

//used by the button pressing to stop the buzzer
void buzzer::BuzzerStop()
{
    bStop = true;
    // BuzzerEnd();
}

//Buzzer end when time out or cancelled by button
void buzzer::BuzzerEnd()
{
    bBuzzerOn = true;      //force to off again
    BuzzerOff();
    duration_on = 0;
    duration_off = 0;
    times = 0;
    longInterval = 0;
    longTimes = 0;
    startTime = 0;
}

void buzzer::BuzzerSet(int onDuration, int offDuration, int times)
{
    duration_on = onDuration;
    duration_off = offDuration;
    this->times = times;
    longInterval = 0;
    longTimes = 1;
    // info_displayf("The para is %d, %d, %d, %d, %d\n", this->duration_on, this->duration_off, this->times, this->longInterval, this->longTimes);
}

void buzzer::BuzzerLongSet(int interval, int times)
{
    longInterval = interval;
    this->longTimes = times;
}

void buzzer::BuzzerConfig(int *para)
{
    duration_on = para[0];
    duration_off = para[1];
    this->times = para[2];
    longInterval = para[3];
    longTimes = para[4];
    // info_displayf("The para is %d, %d, %d, %d, %d\n", this->duration_on, this->duration_off, this->times, this->longInterval, this->longTimes);
}

void buzzer::BuzzerAlarm()
{
    int para[] = {500, 300, 3, 1000, 3};
    BuzzerConfig(para);
    BuzzerStart();
}

void buzzer::BuzzerAlert()
{
    int para[] = {1000, 500, 3, 0, 3};
    BuzzerConfig(para);
    BuzzerStart();
}

void buzzer::BuzzerChoose(int type)
{
}

void buzzer::BuzzerOn()
{
    if (bBuzzerOn)
    {
        return;
    }
    if (_ForteSetting.parameter.buzzerOn != 1)      //if configuration is not On
    {
        return;
    }
    _LED.BuzzerOn();
    bBuzzerOn = true;
}

void buzzer::BuzzerOff()
{
    if (!bBuzzerOn)
    {
        return;
    }
    
    _LED.BuzzerOff();
    bBuzzerOn = false;
}

buzzer _buzzer;

