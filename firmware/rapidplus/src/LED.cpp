#include "LED.h"

LED::LED(/* args */)
{
    pinMode(LED_PWM_PORT, OUTPUT);   // sets the pin as output
    analogWrite(LED_PWM_PORT, 0);    // switch off the LED immediately after power on
    pinMode(I2C_RST, OUTPUT);
    digitalWrite(I2C_RST, 1);   //reset all of the I2C devices include LED I/O expander and 2 sensor multiplex
}

LED::~LED()
{
}

void LED::begin()
{
    if (!mcp.begin_I2C()) {
        info_displayln("mcp connection error.");
        _displayCLD.ErrorDisplay("LED IO Expander Error at mcp.begin().\nThis is I2C error, check the connection with I/O expander and sensor board connection");
        delay(10000);
        ESP.restart();
    }
    // configure LED pin for output, and output to turn off the LED
    for (size_t i = 0; i < 10; i++)
    {
        /* code */
        mcp.pinMode(LED_CHANNEL[i], OUTPUT);
        mcp.digitalWrite(LED_CHANNEL[i], LED_OFF);
    }

    //buzzer setting and beep once
    mcp.pinMode(BUZZER, OUTPUT);        //this is to initiate the buzzer as output!
    for (u8_t i = 0; i < 2; i++)
    {
        /* code */
        mcp.digitalWrite(BUZZER, HIGH);
        delay(10);
        mcp.digitalWrite(BUZZER, LOW);  
        // delay(100);
    }
    
}

void LED::LED_PWM_Set(int value)
{
    analogWrite(LED_PWM_PORT, value);    // power on the LED driver when testing start
}

void LED::LED_on(int channel)
{
    uint8_t * LED_PWM_VALUE_SETTING = _ForteSetting.parameter.led_power;
    _LED.LED_PWM_Set((int)LED_PWM_VALUE_SETTING[channel]);
    mcp.digitalWrite(LED_CHANNEL[channel], LED_ON);        // turn on the LED
}

void LED::LED_off(int channel)
{
    mcp.digitalWrite(LED_CHANNEL[channel], LED_OFF);       //turn off the LED
    _LED.LED_PWM_Set(0);     //Switch off the LED driver as well
    delay(10);
}

void LED::LED_OFF_ALL()
{
    for (uint8_t i = 0; i < 10; i++)        //turn off all LED in case some is still open
    {
        mcp.digitalWrite(LED_CHANNEL[i], LED_OFF);
    }
    _LED.LED_PWM_Set(0);     //Switch off the LED driver as well
}

void LED::BuzzerOn()
{
    mcp.digitalWrite(BUZZER, HIGH);
}

void LED::BuzzerOff()
{
    mcp.digitalWrite(BUZZER, LOW);
}

uint8_t LED::getPWMValue(int LEDChannel)
{
    uint8_t * LED_PWM_VALUE_SETTING = _ForteSetting.parameter.led_power;
    return LED_PWM_VALUE_SETTING[LEDChannel];
}

void LED::setPWMValue(int LEDChannel, uint8_t value)
{
    uint8_t * LED_PWM_VALUE_SETTING = _ForteSetting.parameter.led_power;
    LED_PWM_VALUE_SETTING[LEDChannel] = value;
    // analogWrite(LEDChannel, value)
}

LED _LED;