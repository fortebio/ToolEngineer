#include "LED.h"

/***********************************************************************
 * Function: LED()
 * Description: Constructor for the LED class. Sets LED_PWM_PORT as an OUTPUT
 * and writes 0 to switch the LED off immediately at power-on, then drives
 * I2C_RST high to reset all I2C devices (the LED I/O expander and the two
 * sensor multiplexers).
 * pramameter: none
 *  return: none
 */
LED::LED(/* args */)
{
    pinMode(LED_PWM_PORT, OUTPUT); // sets the pin as output
    analogWrite(LED_PWM_PORT, 0);  // switch off the LED immediately after power on
    pinMode(I2C_RST, OUTPUT);
    digitalWrite(I2C_RST, 1); // reset all of the I2C devices include LED I/O expander and 2 sensor multiplex
}

/***********************************************************************
 * Function: ~LED()
 * Description: Destructor for the LED class. No resources to release.
 * pramameter: none
 *  return: none
 */
LED::~LED()
{
}

/***********************************************************************
 * Function: _mcp_digitalWrite()
 * Description: Thin wrapper that writes a value to a pin on the MCP I/O
 * expander via mcp.digitalWrite(). Does not take the I2C mutex itself.
 * pramameter: pin = MCP expander pin to write;
 * pramameter: val = logic level to write to the pin
 *  return: none
 */
void LED::_mcp_digitalWrite(uint8_t pin, uint8_t val)
{
    // Guard: the MCP I/O expander is only initialized in LED::begin()
    // (mcp.begin_I2C()), which runs inside _sensor6035.begin() — AFTER the
    // early init (displayCLD/ForteSetting). If an error screen fires before
    // that (e.g. empty-EEPROM -> ErrorDisplay -> BuzzerAlarm), writing to the
    // uninitialized mcp dereferences a NULL I2C device -> panic/reset loop.
    if (!mcpReady)
        return;
    mcp.digitalWrite(pin, val);
}

/***********************************************************************
 * Function: begin()
 * Description: Initializes the MCP I2C I/O expander. On begin_I2C() failure
 * it logs the error, shows an I2C/IO-expander error on the display, waits
 * 10s, and restarts the ESP. On success it configures all 10 LED_CHANNEL
 * pins as outputs set to LED_OFF, then configures the BUZZER pin as output
 * and beeps it twice.
 * pramameter: none
 *  return: none
 */
void LED::begin()
{
    if (!mcp.begin_I2C())
    {
        info_displayln("mcp connection error.");
        _displayCLD.ErrorDisplay("LED IO Expander Error at mcp.begin().\nThis is I2C error, check the connection with I/O expander and sensor board connection");
        delay(10000);
        ESP.restart();
    }
    mcpReady = true; // I2C expander is up — MCP writes are now safe
    // configure LED pin for output, and output to turn off the LED
    for (size_t i = 0; i < 10; i++)
    {
        /* code */
        mcp.pinMode(LED_CHANNEL[i], OUTPUT);
        mcp.digitalWrite(LED_CHANNEL[i], LED_OFF);
    }

    // buzzer setting and beep once
    mcp.pinMode(BUZZER, OUTPUT); // this is to initiate the buzzer as output!
    for (u8_t i = 0; i < 2; i++)
    {
        /* code */
        mcp.digitalWrite(BUZZER, HIGH);
        delay(10);
        mcp.digitalWrite(BUZZER, LOW);
        // delay(100);
    }
}

/***********************************************************************
 * Function: LED_PWM_Set()
 * Description: Sets the LED driver brightness by writing the PWM duty value
 * to LED_PWM_PORT via analogWrite().
 * pramameter: value = PWM duty value to drive the LED power output
 *  return: none
 */
void LED::LED_PWM_Set(int value)
{
    analogWrite(LED_PWM_PORT, value); // power on the LED driver when testing start
}

/***********************************************************************
 * Function: BuzzerOn()
 * Description: Turns the buzzer on by taking the I2C mutex (with
 * LED_I2C_MUTEX_TIMEOUT_MS timeout), driving the BUZZER MCP pin HIGH, and
 * releasing the mutex.
 * pramameter: none
 *  return: none
 */
void LED::BuzzerOn()
{
    if (gI2CMutex != NULL &&
        xSemaphoreTake(gI2CMutex, pdMS_TO_TICKS(LED_I2C_MUTEX_TIMEOUT_MS)) == pdTRUE)
    {
        _mcp_digitalWrite(BUZZER, HIGH);
        xSemaphoreGive(gI2CMutex);
    }
}

/***********************************************************************
 * Function: BuzzerOff()
 * Description: Turns the buzzer off by taking the I2C mutex (with
 * LED_I2C_MUTEX_TIMEOUT_MS timeout), driving the BUZZER MCP pin LOW, and
 * releasing the mutex.
 * pramameter: none
 *  return: none
 */
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

/***********************************************************************
 * Function: LED_on_unguarded()
 * Description: Turns on one LED channel WITHOUT taking the I2C mutex; the
 * caller must already hold it (used inside SensorTask). Sets the PWM power
 * from the stored led_power setting for the channel, then drives the
 * channel's MCP pin LED_ON.
 * pramameter: channel = LED channel index to turn on
 *  return: none
 */
void LED::LED_on_unguarded(int channel)
{
    uint8_t *LED_PWM_VALUE_SETTING = _ForteSetting.parameter.led_power;
    _LED.LED_PWM_Set((int)LED_PWM_VALUE_SETTING[channel]);
    _mcp_digitalWrite(LED_CHANNEL[channel], LED_ON);
}

/***********************************************************************
 * Function: LED_off_unguarded()
 * Description: Turns off one LED channel WITHOUT taking the I2C mutex; the
 * caller must already hold it. Drives the channel's MCP pin LED_OFF, sets
 * the PWM power to 0, and delays 10ms.
 * pramameter: channel = LED channel index to turn off
 *  return: none
 */
void LED::LED_off_unguarded(int channel)
{
    _mcp_digitalWrite(LED_CHANNEL[channel], LED_OFF);
    _LED.LED_PWM_Set(0);
    delay(10);
}

/***********************************************************************
 * Function: getPWMValue()
 * Description: Returns the stored LED PWM power setting for a channel from
 * _ForteSetting.parameter.led_power.
 * pramameter: LEDChannel = LED channel index to query
 *  return: the stored led_power PWM value (uint8_t) for that channel
 */
uint8_t LED::getPWMValue(int LEDChannel)
{
    uint8_t *LED_PWM_VALUE_SETTING = _ForteSetting.parameter.led_power;
    return LED_PWM_VALUE_SETTING[LEDChannel];
}

/***********************************************************************
 * Function: setPWMValue()
 * Description: Stores a new LED PWM power setting for a channel into
 * _ForteSetting.parameter.led_power.
 * pramameter: LEDChannel = LED channel index to update;
 * pramameter: value = new led_power PWM value to store for that channel
 *  return: none
 */
void LED::setPWMValue(int LEDChannel, uint8_t value)
{
    uint8_t *LED_PWM_VALUE_SETTING = _ForteSetting.parameter.led_power;
    LED_PWM_VALUE_SETTING[LEDChannel] = value;
}

/***********************************************************************
 * Function: LED_OFF_ALL_unguarded()
 * Description: Turns off all 10 LED channels WITHOUT taking the I2C mutex;
 * the caller must already hold it. Drives every LED_CHANNEL pin LED_OFF and
 * sets the PWM power to 0.
 * pramameter: none
 *  return: none
 */
void LED::LED_OFF_ALL_unguarded()
{
    for (uint8_t i = 0; i < 10; i++)
    {
        _mcp_digitalWrite(LED_CHANNEL[i], LED_OFF);
    }
    _LED.LED_PWM_Set(0);
}

LED _LED;