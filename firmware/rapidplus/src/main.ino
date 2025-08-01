/*
Version 1.3 note: add function send wifi id and password via bluetooth
Version 1.4 note: add function sellect language
*/

#include "define.h"
#include "displayCLD.h"
#include "button.h"
#include "bluetooth.h"
#include "thermometer.h"
#include "wire.h"
#include "sensor6035.h"
#include "PIDControl.h"
#include "ForteSetting.h"
#include "Fan.h"
// #include <ESPmDNS.h>

buttonManager _buttonManager;

WebServer server(80);

void setup()
{
  Serial.setRxBufferSize(3 * 1024);
  Serial.begin(115200);
  // configure the I2C IO
  Wire.begin(SDA_Forte, SCL_Forte);
  // configure the button
  _buttonManager.buttonStart();

  // load ssid, password, id_device id from EEPROM
  loadSettingDevice();

  WiFi.begin(ssid.c_str(), password.c_str());
  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 20)
  {
    delay(50); // đợi 50ms mỗi lần
    retries++;
    Serial.print(".");
  }
  delay(100);

  // if (WiFi.status() == WL_CONNECTED)
  // {
  // if (!MDNS.begin("rpl"))
  // {
  //   info_displayln("Error setting up MDNS responder!");
  //   while (1)
  //   {
  //     delay(1000);
  //   }
  // }
  // MDNS.addService("http", "tcp", 80);
  // }

  _displayCLD.begin();
  _ForteSetting.begin();
  _PIDControl.begin();
  _displayCLD.logoFortebiotech();
  _sensor6035.begin(); // include sensor, LED and buzzer!
  _Fan.begin();
  _PIDControl.timeoutSetting();

  postData_Chart();
}

void loop()
{
  _PIDControl.loop();
  _sensor6035.loop();
  _displayCLD.loop();
  _ForteSetting.loop(); // configure para
  _buzzer.loop();
  _Fan.loop(); // keep open the Fan

  server.handleClient();
}
