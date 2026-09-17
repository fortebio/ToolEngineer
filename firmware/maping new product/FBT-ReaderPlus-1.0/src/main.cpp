#include "define.h"
#include "sensor.h"
#include "displayLCD.h"
#include "button.h"
#include "bluetooth.h"

void setup()
{
  Serial.begin(115200);
  loadCredentialsFromEEPROM();

  WiFi.begin(ssid.c_str(), password.c_str());
  ip = WiFi.localIP().toString().c_str();

  _sensor.begin();
  _displayLCD.begin();
  _displayLCD.logoFortebiotech();
}

void loop()
{
  _displayLCD.loop();
  _sensor.loop();
}
