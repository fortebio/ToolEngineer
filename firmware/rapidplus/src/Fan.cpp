#include "Fan.h"



Fan::Fan(/* args */)
{
}

Fan::~Fan()
{
}

void Fan::begin()
{
    pinMode(FANIO, OUTPUT);   // sets the pin as output
    digitalWrite(FANIO, HIGH);     //Turn on the Fan
    // analogWrite(FANIO, 0x50);
}

void Fan::loop()
{
    //set here in case the Fan is on/off based on the tempeature, or adjust the speed via PWM
    digitalWrite(FANIO, HIGH);     //Turn on the Fan
}

void Fan::FanStart()
{
    digitalWrite(FANIO, HIGH);     //Turn on the Fan
}

void Fan::FanStop()
{
    digitalWrite(FANIO, LOW);     //Turn off the Fan
}

Fan _Fan;
