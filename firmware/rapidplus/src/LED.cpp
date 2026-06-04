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

void LED::_mcp_digitalWrite(uint8_t pin, uint8_t val)
{
    mcp.digitalWrite(pin, val);
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
    uint8_t *LED_PWM_VALUE_SETTING = _ForteSetting.parameter.led_power;
    _LED.LED_PWM_Set((int)LED_PWM_VALUE_SETTING[channel]); // GPIO, no mutex

    if (gI2CMutex != NULL &&
        xSemaphoreTake(gI2CMutex, pdMS_TO_TICKS(LED_I2C_MUTEX_TIMEOUT_MS)) == pdTRUE)
    {
        _mcp_digitalWrite(LED_CHANNEL[channel], LED_ON);
        xSemaphoreGive(gI2CMutex);
    }
    else
    {
        Serial.println("LED_on: I2C mutex timeout");
    }
}

void LED::LED_off(int channel)
{
    if (gI2CMutex != NULL &&
        xSemaphoreTake(gI2CMutex, pdMS_TO_TICKS(LED_I2C_MUTEX_TIMEOUT_MS)) == pdTRUE)
    {
        _mcp_digitalWrite(LED_CHANNEL[channel], LED_OFF);
        xSemaphoreGive(gI2CMutex);
    }
    _LED.LED_PWM_Set(0); // GPIO, after release
    delay(10);
}

void LED::LED_OFF_ALL()
{
    if (gI2CMutex != NULL &&
        xSemaphoreTake(gI2CMutex, pdMS_TO_TICKS(LED_I2C_MUTEX_TIMEOUT_MS)) == pdTRUE)
    {
        for (uint8_t i = 0; i < 10; i++)
        {
            _mcp_digitalWrite(LED_CHANNEL[i], LED_OFF);
        }
        xSemaphoreGive(gI2CMutex);
    }
    _LED.LED_PWM_Set(0);
}

void LED::BuzzerOn()
{
    if (gI2CMutex != NULL &&
        xSemaphoreTake(gI2CMutex, pdMS_TO_TICKS(LED_I2C_MUTEX_TIMEOUT_MS)) == pdTRUE)
    {
        _mcp_digitalWrite(BUZZER, HIGH);
        xSemaphoreGive(gI2CMutex);
    }
}

void LED::BuzzerOff()
{
    if (gI2CMutex != NULL &&
        xSemaphoreTake(gI2CMutex, pdMS_TO_TICKS(LED_I2C_MUTEX_TIMEOUT_MS)) == pdTRUE)
    {
        _mcp_digitalWrite(BUZZER, LOW);
        xSemaphoreGive(gI2CMutex);
    }
}

// ==========================================
// UNGUARDED methods — caller MUST hold mutex
// Used inside SensorTask where mutex is already held
// ==========================================

void LED::LED_on_unguarded(int channel)
{
    uint8_t *LED_PWM_VALUE_SETTING = _ForteSetting.parameter.led_power;
    _LED.LED_PWM_Set((int)LED_PWM_VALUE_SETTING[channel]);
    _mcp_digitalWrite(LED_CHANNEL[channel], LED_ON);
}

void LED::LED_off_unguarded(int channel)
{
    _mcp_digitalWrite(LED_CHANNEL[channel], LED_OFF);
    _LED.LED_PWM_Set(0);
    delay(10);
}

uint8_t LED::getPWMValue(int LEDChannel)
{
    uint8_t *LED_PWM_VALUE_SETTING = _ForteSetting.parameter.led_power;
    return LED_PWM_VALUE_SETTING[LEDChannel];
}

void LED::setPWMValue(int LEDChannel, uint8_t value)
{
    uint8_t *LED_PWM_VALUE_SETTING = _ForteSetting.parameter.led_power;
    LED_PWM_VALUE_SETTING[LEDChannel] = value;
}

void LED::LED_OFF_ALL_unguarded()
{
    for (uint8_t i = 0; i < 10; i++)
    {
        _mcp_digitalWrite(LED_CHANNEL[i], LED_OFF);
    }
    _LED.LED_PWM_Set(0);
}

LED _LED;