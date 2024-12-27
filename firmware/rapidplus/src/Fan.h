#ifndef _FAN_H
#define _FAN_H

#include "define.h"

class Fan
{
private:
    /* data */
public:
    Fan(/* args */);
    ~Fan();

    void begin();
    void loop();
    void FanStart();
    void FanStop();

};
extern Fan _Fan;
#endif