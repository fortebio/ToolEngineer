/*
Version 1.3 note: add function send wifi id and password via bluetooth
Version 1.4 note: add function sellect language
*/

#include "define.h"
#include "displayCLD.h"
#include "button.h"
#include "Bluetooth.h"
#include "thermometer.h"
#include "Wire.h"
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

  _displayCLD.begin();
  _ForteSetting.begin();
  _PIDControl.begin();
  _displayCLD.logoFortebiotech();
  _sensor6035.begin(); // include sensor, LED and buzzer!
  _Fan.begin();
  _PIDControl.timeoutSetting();

  postData_Chart();
  checkFirmware();
}

void loop()
{
  if (flag_check_Update == true)
  {
    _displayCLD.type_infor = eUpdateOTA;
  }
  
  _PIDControl.loop();
  _sensor6035.loop();
  _displayCLD.loop();
  _ForteSetting.loop(); // configure para
  _buzzer.loop();
  _Fan.loop(); // keep open the Fan

  server.handleClient();
  updateFirmware();
}
