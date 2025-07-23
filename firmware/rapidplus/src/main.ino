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

  //load ssid, password, id_device id from EEPROM
  loadSettingDevice();

  //WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());
  /*if (!MDNS.begin("rapidplus"))
  {
    info_displayln("Error setting up MDNS responder!");
    while (1) {
      delay(10);
    }
  }
  info_displayln("MDNS Started");
  */

  // Read_language_fromEEPROM();

  _displayCLD.begin();
  _ForteSetting.begin();
  _PIDControl.begin();
  _displayCLD.logoFortebiotech();
  _sensor6035.begin(); // include sensor, LED and buzzer!
  _Fan.begin();
  _PIDControl.timeoutSetting();

  postData_Chart();


  // info_displayf("This is the Forte Heater&Reader %s on PCB %s @ %s %s\n", FirmwareVer, _ForteSetting.parameter.PCB_version, __DATE__, __TIME__);
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
