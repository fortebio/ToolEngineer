#include "Fan.h"

/***********************************************************************
 * Function: Fan()
 * Description: Constructor for the Fan class. Performs no initialization;
 * hardware setup is deferred to begin().
 * pramameter: none
 *  return: none
 */
Fan::Fan(/* args */)
{
}

/***********************************************************************
 * Function: ~Fan()
 * Description: Destructor for the Fan class. No resources to release.
 * pramameter: none
 *  return: none
 */
Fan::~Fan()
{
}

/***********************************************************************
 * Function: begin()
 * Description: Initializes the fan GPIO by setting FANIO as an OUTPUT and
 * driving it HIGH to turn the fan on.
 * pramameter: none
 *  return: none
 */
void Fan::begin()
{
    pinMode(FANIO, OUTPUT);    // sets the pin as output
    digitalWrite(FANIO, HIGH); // Turn on the Fan
    // digitalWrite(FANIO, PWM_OFF);
    // analogWrite(FANIO, 0x50);
}

/***********************************************************************
 * Function: loop()
 * Description: Periodic fan service. Currently drives FANIO HIGH to keep the
 * fan on; a placeholder for future temperature-based on/off or PWM speed
 * control.
 * pramameter: none
 *  return: none
 */
void Fan::loop()
{
    // set here in case the Fan is on/off based on the tempeature, or adjust the speed via PWM
    digitalWrite(FANIO, HIGH); // Turn on the Fan
    //  digitalWrite(FANIO, PWM_FULL);     //Turn on the Fan
}

/***********************************************************************
 * Function: FanStart()
 * Description: Turns the fan on by driving the FANIO pin HIGH.
 * pramameter: none
 *  return: none
 */
void Fan::FanStart()
{
    digitalWrite(FANIO, HIGH); // Turn on the Fan
    // digitalWrite(FANIO, PWM_FULL);     //Turn on the Fan
}

/***********************************************************************
 * Function: FanStop()
 * Description: Turns the fan off by driving the FANIO pin LOW.
 * pramameter: none
 *  return: none
 */
void Fan::FanStop()
{
    digitalWrite(FANIO, LOW); // Turn off the Fan
    // digitalWrite(FANIO, PWM_OFF);     //Turn off the Fan
}

Fan _Fan;
