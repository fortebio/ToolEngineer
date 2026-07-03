#ifndef _LED_H
#define _LED_H

#include "define.h"
// #include <Adafruit_MCP23X08.h>
#include <Adafruit_MCP23X17.h>
#include "ForteSetting.h"

#define LED_ON LOW   // LED on is LOW, may change it in the future
#define LED_OFF HIGH // LED off is HIGH, may change it in the future

#define LED_I2C_MUTEX_TIMEOUT_MS 30

class LED
{
private:
    /* data */
    Adafruit_MCP23X17 mcp;
    bool mcpReady = false; // true only after mcp.begin_I2C() has succeeded
    const int LED_CHANNEL[10] = {LED0, LED1, LED2, LED3, LED4, LED5, LED6, LED7, LED8, LED9};

    void _mcp_digitalWrite(uint8_t pin, uint8_t val);

public:
    LED(/* args */);
    ~LED();
    void begin();
    void LED_PWM_Set(int value);
    void BuzzerOn();
    void BuzzerOff();
    uint8_t getPWMValue(int LEDChannel);
    void setPWMValue(int LEDChannel, uint8_t value);

    void LED_on_unguarded(int channel);
    void LED_off_unguarded(int channel);
    void LED_OFF_ALL_unguarded();
};
extern LED _LED;

#endif