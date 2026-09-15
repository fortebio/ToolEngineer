// /*
// Version 1.3 note: add function send wifi id_BLE and password via bluetooth
// Version 1.4 note: add function sellect language
// version 2.2 note: add feature update threshold
// version 2.4 note: add feature default value for threshold
// version 2.5 note: add feature group data
// Version 2.6 note: add feature update firmware via webserver
// */

#include "define.h"
#include "sensor.h"
#include "displayLCD.h"
#include "button.h"
#include "bluetooth.h"

void setup()
{
    Serial.begin(115200);
    loadCredentialsFromEEPROM();
    initAnimalData();

    WiFi.begin(ssid.c_str(), password.c_str());
    ip = WiFi.localIP().toString().c_str();

    _sensor.begin();
    _displayLCD.begin();
}

void loop()
{
    _displayLCD.loop();
    _sensor.loop();
}