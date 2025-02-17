#ifndef _BUZZER_H
#define _BUZZER_H
#include "define.h"

class buzzer
{
private:
    /* data */
    int duration_on;
    int duration_off;
    int times;
    // bool bRepeat;
    bool bBuzzerOn = false; // record whether buzzer is on or off
    int longInterval;
    int longTimes;
    unsigned long startTime = 0;
    bool bStop = false; // stop flag may be from button or somewhere else

    void BuzzerOn();  // only set to on
    void BuzzerOff(); // only set to off
public:
    buzzer(/* args */);
    ~buzzer();

    // void begin();     //supportred by LED
    void loop();

    void BuzzerStart();
    void BuzzerStop(); // used by the button pressing to stop the buzzer
    void BuzzerEnd();
    void BuzzerSet(int onDuration, int offDuration, int times); // set the on/off time and times
    void BuzzerLongSet(int interval, int times);                // set the long interval between
    void BuzzerConfig(int *para);

    void BuzzerAlarm();
    void BuzzerAlert();

    void BuzzerChoose(int type); // choose one of the default types
};

extern buzzer _buzzer;

#endif