/*
Support on and off
Support loop detection
Support auto configuration, the frequency and duration

*/
#include "buzzer.h"
#include "LED.h"



/***********************************************************************
 * Function: buzzer()
 * Description: Constructor for the buzzer class. Currently performs no
 *  initialization (pin setup is commented out).
 * pramameter: none
 *  return: none
 */
buzzer::buzzer(/* args */)
{
    // pinMode(BUZZER, OUTPUT);
    // digitalWrite(BUZZER, 1);
}

/***********************************************************************
 * Function: ~buzzer()
 * Description: Destructor for the buzzer class. No cleanup is required.
 * pramameter: none
 *  return: none
 */
buzzer::~buzzer()
{
}

/***********************************************************************
 * Function: buzzer::loop()
 * Description: Non-blocking state machine driven by millis(). Handles a stop
 *  request, then computes the current position within the repeating pattern
 *  (times of on/off cycles plus a long interval, repeated longTimes). Turns
 *  the buzzer on during on-phases, off during off-phases and the long
 *  interval, and ends the buzzer when the whole sequence completes.
 * pramameter: none
 *  return: none
 */
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

/***********************************************************************
 * Function: buzzer::BuzzerStart()
 * Description: Begins a configured buzzer sequence by recording the start
 *  time and turning the buzzer on. Rejects the start (with a log message) if
 *  duration_on exceeds 10000ms or the repeat count (times) exceeds 30.
 * pramameter: none
 *  return: none
 */
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
/***********************************************************************
 * Function: buzzer::BuzzerStop()
 * Description: Requests the buzzer to stop by setting the bStop flag, which
 *  is acted upon on the next loop() call (typically triggered by a button
 *  press).
 * pramameter: none
 *  return: none
 */
void buzzer::BuzzerStop()
{
    bStop = true;
    // BuzzerEnd();
}

//Buzzer end when time out or cancelled by button
/***********************************************************************
 * Function: buzzer::BuzzerEnd()
 * Description: Terminates an active buzzer sequence: forces the buzzer off
 *  and resets all timing/pattern state (duration_on, duration_off, times,
 *  longInterval, longTimes, startTime) to zero. Called on timeout or button
 *  cancel.
 * pramameter: none
 *  return: none
 */
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

/***********************************************************************
 * Function: buzzer::BuzzerSet()
 * Description: Configures a simple single-session beep pattern: sets the on
 *  and off durations and repeat count, with no long interval (longInterval=0)
 *  and a single long cycle (longTimes=1).
 * pramameter: onDuration - buzzer on time in ms per beep
 * pramameter: offDuration - buzzer off time in ms between beeps
 * pramameter: times - number of on/off beeps in the session
 *  return: none
 */
void buzzer::BuzzerSet(int onDuration, int offDuration, int times)
{
    duration_on = onDuration;
    duration_off = offDuration;
    this->times = times;
    longInterval = 0;
    longTimes = 1;
    // info_displayf("The para is %d, %d, %d, %d, %d\n", this->duration_on, this->duration_off, this->times, this->longInterval, this->longTimes);
}

/***********************************************************************
 * Function: buzzer::BuzzerLongSet()
 * Description: Configures the repeating "long" pattern parameters: the gap
 *  (longInterval) inserted after each beep session and how many times
 *  (longTimes) the whole session repeats.
 * pramameter: interval - the long interval gap in ms after each session
 * pramameter: times - the number of times the session repeats (longTimes)
 *  return: none
 */
void buzzer::BuzzerLongSet(int interval, int times)
{
    longInterval = interval;
    this->longTimes = times;
}

/***********************************************************************
 * Function: buzzer::BuzzerConfig()
 * Description: Configures the full buzzer pattern from a 5-element array:
 *  para[0]=duration_on, para[1]=duration_off, para[2]=times,
 *  para[3]=longInterval, para[4]=longTimes.
 * pramameter: para - pointer to an int array of 5 pattern parameters
 *  return: none
 */
void buzzer::BuzzerConfig(int *para)
{
    duration_on = para[0];
    duration_off = para[1];
    this->times = para[2];
    longInterval = para[3];
    longTimes = para[4];
    // info_displayf("The para is %d, %d, %d, %d, %d\n", this->duration_on, this->duration_off, this->times, this->longInterval, this->longTimes);
}

/***********************************************************************
 * Function: buzzer::BuzzerAlarm()
 * Description: Plays the alarm pattern: 3 beeps of 500ms on / 300ms off,
 *  followed by a 1000ms interval, repeated 3 times, then starts it.
 * pramameter: none
 *  return: none
 */
void buzzer::BuzzerAlarm()
{
    int para[] = {500, 300, 3, 1000, 3};
    BuzzerConfig(para);
    BuzzerStart();
}

/***********************************************************************
 * Function: buzzer::BuzzerAlert()
 * Description: Plays the alert pattern: 3 beeps of 1000ms on / 500ms off
 *  with no long interval, repeated 3 times, then starts it.
 * pramameter: none
 *  return: none
 */
void buzzer::BuzzerAlert()
{
    int para[] = {1000, 500, 3, 0, 3};
    BuzzerConfig(para);
    BuzzerStart();
}

/***********************************************************************
 * Function: buzzer::BuzzerChoose()
 * Description: Intended to select a buzzer pattern by type; currently an
 *  empty stub with no implementation.
 * pramameter: type - the pattern type to choose (unused)
 *  return: none
 */
void buzzer::BuzzerChoose(int type)
{
}

/***********************************************************************
 * Function: buzzer::BuzzerOn()
 * Description: Physically turns the buzzer on via _LED.BuzzerOn(), but only
 *  if it is not already on and the global buzzerOn setting is enabled (==1);
 *  updates the bBuzzerOn state flag.
 * pramameter: none
 *  return: none
 */
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

/***********************************************************************
 * Function: buzzer::BuzzerOff()
 * Description: Physically turns the buzzer off via _LED.BuzzerOff() if it is
 *  currently on, and clears the bBuzzerOn state flag.
 * pramameter: none
 *  return: none
 */
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

