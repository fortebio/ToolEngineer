#include "displayCLD.h"
#include <U8g2lib.h>
#include "displayresources.h"
#include "define.h"
#include "Adafruit_GFX.h"
#include "Adafruit_ILI9341.h"
// #include "sensor.h"
#include "sensor6035.h"
// #include "update_firmware.h"
#include "Bluetooth.h"
#include "PIDControl.h"
#include <string>
// #include "sensor6035.h"

#define Forte_Green 0x25F8
#define VIOLET 0xA81F
String measure_value = "";

// bool butt = 1;  // 0: blue, 1: green

displayCLD::displayCLD(/* args */)
{
  this->bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCK, TFT_MOSI, TFT_MISO);
  this->display = new Arduino_ILI9341(this->bus, TFT_RESET);
}

displayCLD::~displayCLD()
{
}

void displayCLD::begin()
{
  this->display->begin();
  this->display->fillScreen(BLACK);
  this->display->setRotation(1);
  this->display->setUTF8Print(true);
}

void displayCLD::logoFortebiotech()
{
  this->display->fillScreen(BLACK);
  this->display->fillTriangle(80, 60, 132, 30, 132, 90, this->display->color565(16, 55, 50));
  this->display->fillTriangle(130, 100, 78, 70, 78, 130, this->display->color565(16, 55, 50));
  this->display->fillTriangle(88, 140, 132, 110, 132, 170, this->display->color565(16, 45, 20));
  this->display->fillTriangle(142, 30, 252, 10, 142, 68, this->display->color565(10, 30, 35));
  this->display->fillTriangle(142, 140, 142, 170, 192, 130, this->display->color565(16, 65, 30));
  this->display->setFont(u8g2_font_unifont_t_vietnamese1);
  this->display->setTextSize(2);
  this->display->setTextColor(this->display->color565(16, 55, 70));
  this->display->setCursor(150, 90);
  this->display->print("FORTE");
  this->display->setCursor(150, 120);
  this->display->print("BIOTECH");
  this->display->setFont(u8g2_font_helvB08_tf);
  this->display->setTextColor(Forte_Green);
  this->display->setTextSize(1);
  this->display->setCursor(80, 190);
  this->display->print("TEST   PRAWNS   WITH   RAPID");
  this->display->setCursor(100, 220);
  this->display->print("PROFIT   NO   LIMIT");
  this->display->setCursor(15, 230);
  this->display->print(FirmwareVer);
  this->display->setFont(u8g2_font_unifont_t_vietnamese1);
  delay(LOGODISPLAYTIME);
  // dbg_display("logo thanh cong");
}

void show_IconWifi(void)
{
  if (WiFi.status() == WL_CONNECTED)
  {
    _displayCLD.display->drawBitmap(286, 9, image_WIFI_Connect, 19, 16, WHITE);
  }
  else
  {
    _displayCLD.display->drawBitmap(286, 9, image_WIFI_Disconnect, 19, 16, WHITE);
  }
}

void show_IconBluetooth(void)
{
  // EEPROM.begin(_EEPROM_SIZE);
  // EEPROM.get(ADDR_CHECK_BT, status_BT);
  // EEPROM.end();
  static bool turnOn_BT = false; // turn off BT when Process is runing and save BT state to turn off once
  if (_displayCLD.type_infor == escreenResult || _displayCLD.type_infor == escreenStart)
  {
    // EEPROM.begin(_EEPROM_SIZE);
    // EEPROM.get(ADDR_CHECK_BT, turnOn_BT);
    turnOn_BT = true;
    // _displayCLD.display->drawBitmap(266, 8, image_BT_Connect, 14, 16, WHITE);
  }
  else
  {
    if (turnOn_BT == true)
    {
      // Serial.println("Truoc khi BT ngat ket noi: " + String(ESP.getFreeHeap()));
      turnOn_BT = false;
      /* turn off bluetooth*/
      SerialBT.end();
      // Serial.println("Sau khi BT ngat ket noi: " + String(ESP.getFreeHeap()));
    }
    // _displayCLD.display->drawBitmap(266, 8, image_BT_Disconnect, 14, 16, WHITE);
  }
}
void displayWaitingUpData(void)
{
  _displayCLD.display->fillScreen(BLACK);
  _displayCLD.display->setTextSize(2);
  _displayCLD.display->setTextColor(WHITE);
  _displayCLD.display->setCursor(50, 100);
  _displayCLD.display->print("Data Uploading...!");
  _displayCLD.display->setTextSize(1);
  _displayCLD.display->setCursor(50, 130);
  _displayCLD.display->print("Please wait...");
  show_IconWifi();
  delay(100);
}

void displayCLD::screen_Start()
{
  if (language == 0)
  {
    ip = WiFi.localIP().toString().c_str(); // Taking ip address
    this->display->fillScreen(BLACK);
    this->display->setTextSize(1);
    this->display->fillRect(108, 0, 108, 20, Forte_Green);
    // this->display->drawRoundRect(15, 0, 302, 240, 10, Forte_Green);
    this->display->setCursor(110, 15);
    this->display->setTextColor(BLACK);
    this->display->println("FORTE BIOTECH");
    this->display->drawBitmap(18, 5, logoFBT, 35, 34, Forte_Green);
    this->display->drawBitmap(275, 200, shrimp, 35, 29, Forte_Green);
    this->display->setTextSize(2);
    this->display->setCursor(80, 100);
    this->display->drawCircle(42, 110, 25, GREEN);
    this->display->fillCircle(42, 110, 20, GREEN);
    this->display->setTextColor(GREEN);
    this->display->println("Nhấn nút xanh");
    this->display->setCursor(100, 140);
    this->display->print("để bắt đầu");
    this->display->setTextSize(1);
    this->display->setCursor(20, 230);
    this->display->print(ip);
    info_displayln(ip);
    this->display->setCursor(20, 210);
    this->display->print("ID " + id);
  }
  else

  {
    ip = WiFi.localIP().toString().c_str(); // Taking ip address
    this->display->fillScreen(BLACK);
    this->display->setTextSize(1);
    this->display->fillRect(108, 0, 108, 20, Forte_Green);
    // this->display->drawRoundRect(10, 0, 302, 240, 10, Forte_Green);
    this->display->setCursor(110, 15);
    this->display->setTextColor(BLACK);
    this->display->println("FORTE BIOTECH");
    this->display->drawBitmap(18, 10, logoFBT, 35, 34, Forte_Green);
    this->display->drawBitmap(275, 200, shrimp, 35, 29, Forte_Green);

    this->display->setTextSize(2);
    this->display->setCursor(80, 70);
    this->display->drawCircle(42, 80, 25, GREEN);
    this->display->fillCircle(42, 80, 20, GREEN);
    this->display->setTextColor(GREEN);
    this->display->println("Press Green:");
    this->display->setCursor(80, 110);
    this->display->print("Lysis");

    this->display->setTextSize(2);
    this->display->setCursor(80, 150);
    this->display->drawCircle(42, 155, 25, RED);
    this->display->fillCircle(42, 155, 20, RED);
    this->display->setTextColor(RED);
    this->display->println("Press Red:");
    this->display->setCursor(80, 180);
    this->display->print("Amplification");

    this->display->setTextSize(1);
    this->display->setTextColor(Forte_Green);
    // this->display->setCursor(20, 230);
    // this->display->print(FirmwareVer + "  " + ip);
    info_displayln(ip);
    // this->display->setCursor(20, 210);
    // this->display->print("ID " + id);

    this->display->setCursor(20, 210);
    this->display->print(ip);
    this->display->setCursor(20, 230);
    this->display->print(FirmwareVer);
  }
}

void displayCLD::ErrorProcessatBegin(String strDescript, String strValue)
{
  this->display->fillScreen(BLACK);
  this->display->setTextSize(2);
  this->display->setTextColor(RED);
  this->display->setCursor(15, 60);
  this->display->print(strDescript);
  this->display->drawRect(30, 140, 272, 80, RED);
  this->display->drawRect(29, 139, 274, 82, RED);
  for (int i = 18; i <= 310; i += 10)
  {
    static int x1 = 0, y1 = 100, x2 = 10, y2 = 110, y3 = 120;
    this->display->drawLine(x1 + i, y1, x2 + i, y2, PINK);
    this->display->drawLine(x1 + i, y3, x2 + i, y2, PINK);
  }
  this->display->drawCircle(55, 180, 22, RED);
  this->display->fillCircle(55, 180, 17, RED);
  this->display->setTextSize(2);
  this->display->setTextColor(RED);

  this->display->setCursor(90, 190);
  this->display->print("Slot " + strValue); // show the tempeature of heater1 and hotlid1
  _buzzer.BuzzerAlarm();
}

void displayCLD::ErrorDisplay(String strDescript)
{
  this->display->fillScreen(BLACK);
  this->display->setTextSize(1);
  this->display->setTextColor(WHITE);
  this->display->setCursor(0, 60);
  this->display->print(strDescript);
  _buzzer.BuzzerAlarm();
  this->display->setTextSize(2);
}

void displayCLD::ErrorProcess(String strDescript, String strValue)
{
  if (type_infor != errprocess)
  {

    this->display->fillScreen(BLACK);
    this->display->setTextSize(2);
    this->display->setTextColor(RED);
    this->display->setCursor(15, 60);
    this->display->print(strDescript);
    this->display->drawRect(30, 140, 272, 80, RED);
    this->display->drawRect(29, 139, 274, 82, RED);
    for (int i = 18; i <= 310; i += 10)
    {
      static int x1 = 0, y1 = 100, x2 = 10, y2 = 110, y3 = 120;
      this->display->drawLine(x1 + i, y1, x2 + i, y2, PINK);
      this->display->drawLine(x1 + i, y3, x2 + i, y2, PINK);
    }
    this->display->drawCircle(55, 180, 22, RED);
    this->display->fillCircle(55, 180, 17, RED);
    this->display->setTextSize(2);
    this->display->setTextColor(RED);

    this->display->setCursor(90, 190);
    this->display->print(strValue); // show the tempeature of heater1 and hotlid1
    // info_displayf("status of LCD is %d\n", type_infor);

    this->display->setTextColor(WHITE);
    this->display->setTextSize(1);
    this->display->setCursor(30, 230); // start position of each sensor value
    this->display->printf("Please restart power");
    _buzzer.BuzzerAlarm();
    type_infor = errprocess;
    timeRefresh = millis() + 1000; // err will display for 1 seconds
  }
}

// when press white button to reboot, or restart next testing after result display
void displayCLD::RestartProcess(String strDescript, String strValue)
{
  this->display->fillScreen(BLACK);
  this->display->setTextSize(2);
  this->display->setTextColor(RED);
  this->display->setCursor(15, 60);
  this->display->print(strDescript);
  this->display->drawRect(30, 140, 272, 80, RED);
  this->display->drawRect(29, 139, 274, 82, RED);
  for (int i = 18; i <= 310; i += 10)
  {
    static int x1 = 0, y1 = 100, x2 = 10, y2 = 110, y3 = 120;
    this->display->drawLine(x1 + i, y1, x2 + i, y2, PINK);
    this->display->drawLine(x1 + i, y3, x2 + i, y2, PINK);
  }
  this->display->drawCircle(55, 180, 22, RED);
  this->display->fillCircle(55, 180, 17, RED);
  this->display->setTextSize(2);
  this->display->setTextColor(RED);

  this->display->setCursor(90, 190);
  this->display->print(strValue); // show the tempeature of heater1 and hotlid1
  // info_displayf("status of LCD is %d\n", type_infor);

  this->display->setTextColor(GREEN);
  this->display->setTextSize(1);
  this->display->setCursor(30, 230); // start position of each sensor value
  this->display->printf("Reboot in 1 second");
  _buzzer.BuzzerAlert();
  type_infor = ewaitingtimeout;
  timeRefresh = millis() + 1000; // err will display for 1 seconds
}

bool displayCLD::ErrorStatus()
{
  return type_infor == errprocess; // return the status whether it's error process or not
}

bool displayCLD::FinishStatus()
{
  return _displayCLD.type_infor == escreenFinished;
}

void displayCLD::TemperatureBottomSeqDisplay()
{
  this->display->fillScreen(BLACK);
  this->display->setTextSize(2);
  this->display->setTextColor(RED);
  this->display->setCursor(10, 10);
  this->display->print("Sequence of bottom sensor");
  this->display->setTextColor(Forte_Green);
  this->display->setCursor(10, 60);
  this->display->printf("%.2f:%.2f:%.2f", _bottomThermometer.getTemperature()[0], _bottomThermometer.getTemperature()[1], _bottomThermometer.getTemperature()[2]); // display the reading from the sensor
  this->display->setTextColor(BLUE);
  this->display->setCursor(10, 110);
  uint8_t *seq = _ForteSetting.parameter.bottomTemperatureSensorSq;
  this->display->printf("%d:%d:%d", seq[0], seq[1], seq[2]);
  this->display->setTextColor(WHITE);
  this->display->setCursor(10, 180);
  this->display->setTextSize(1);
  this->display->printf("Press white button to skip and simulate\n");
}

void displayCLD::TemperatureTopSeqDisplay()
{
  this->display->fillScreen(BLACK);
  this->display->setTextSize(2);
  this->display->setTextColor(RED);
  this->display->setCursor(10, 10);
  this->display->print("Sequence of top sensor");
  this->display->setTextColor(Forte_Green);
  this->display->setCursor(10, 60);
  this->display->printf("%.2f:%.2f:%.2f", _topThermometer.getTemperature()[0], _topThermometer.getTemperature()[1], _topThermometer.getTemperature()[2]); // display the reading from the sensor
  this->display->setTextColor(BLUE);
  this->display->setCursor(10, 110);
  uint8_t *seq = _ForteSetting.parameter.topTemperatureSensorSq;
  this->display->printf("%d:%d:%d", seq[0], seq[1], seq[2]);
  this->display->setTextColor(WHITE);
  this->display->setCursor(10, 180);
  this->display->setTextSize(1);
  this->display->printf("Press white button to skip and simulate\n");
}

void displayCLD::NextTestDisplay()
{
  this->display->fillScreen(BLACK);
  this->display->setTextSize(2);
  this->display->setTextColor(RED);
  this->display->setCursor(42, 60);
  this->display->print("Test next");
  type_infor = ewaitingtimeout;
  timeRefresh = millis() + 1000;
}

void displayCLD::ErrRebootDisplay()
{
  this->display->fillScreen(BLACK);
  this->display->setTextSize(2);
  this->display->setTextColor(RED);
  this->display->setCursor(42, 60);
  this->display->print("Restart testing");
  timeRefresh = millis() + 1000;
  type_infor = ewaitingtimeout;
}

void displayCLD::preHeat67CLD_Header()
{

  if (bheadershow)
  {
    // info_displayln("header show");
    this->display->fillScreen(BLACK);
    this->display->setTextSize(2);
    this->display->setTextColor(Forte_Green);
    this->display->setCursor(15, 60);
    this->display->print("Preheat heater and"); // update from 67, as the target temperature of liquid is 65 degree
    this->display->setCursor(15, 90);
    this->display->print("sensor in 15mins");
    this->display->drawRect(30, 140, 272, 80, RED);
    this->display->drawRect(29, 139, 274, 82, RED);
    for (int i = 18; i <= 310; i += 10)
    {
      static int x1 = 0, y1 = 100, x2 = 10, y2 = 110, y3 = 120;
      this->display->drawLine(x1 + i, y1, x2 + i, y2, PINK);
      this->display->drawLine(x1 + i, y3, x2 + i, y2, PINK);
    }
    this->display->drawCircle(55, 180, 22, RED);
    this->display->fillCircle(55, 180, 17, RED);
    if (!_sensor6035.getSensorPreheatReady())
    {
      this->display->setTextSize(1);
      this->display->setTextColor(GREEN);
      this->display->setCursor(15, 235);
      this->display->print("Press green to skip sensor preheat");
    }
    else
    {
      if (_PIDControl.getphase2ready()) // check the status of heater
      {
        _displayCLD.type_infor = ewaitampTube;
        _displayCLD.changeScreen = true;
        _buzzer.BuzzerAlert();
      }
    }
    bheadershow = false; // header has been showed
  }
}

void displayCLD::preHeat67CLD()
{
  if (_bottomThermometer.getNewTemperatureScreenFlag()) // if there is temperature data to display
  {
    this->display->fillRect(90, 160, 180, 50, BLACK);
    this->display->setCursor(90, 190);
    double *temperature = _PIDControl.getBottomTemperature();
    double *temperatureHotlid = _PIDControl.getHotlidTemperature();
    this->display->setTextSize(2);
    if (_PIDControl.getphase2ready())
    {
      this->display->setTextColor(Forte_Green);
    }
    else
    {
      this->display->setTextColor(RED);
    }
    this->display->printf("%d:%d:%d:%d", int(temperature[1]), int(temperature[2]), int(temperatureHotlid[0]), int(temperatureHotlid[1]));
    _bottomThermometer.clearNewTemperatureScreenFlag(); // clear the flag after display the temperature
  }
}

void displayCLD::preHeat80CLD_Header()
{
  if (bheadershow)
  {
    this->display->fillScreen(BLACK);
    this->display->setTextSize(2);
    this->display->setTextColor(Forte_Green);
    this->display->setCursor(15, 60);
    this->display->print("Heat up to 80");
    this->display->setCursor(15, 90);
    this->display->print("in about 10min");
    this->display->drawRect(30, 140, 272, 80, RED);
    this->display->drawRect(29, 139, 274, 82, RED);
    for (int i = 18; i <= 310; i += 10)
    {
      static int x1 = 0, y1 = 100, x2 = 10, y2 = 110, y3 = 120;
      this->display->drawLine(x1 + i, y1, x2 + i, y2, PINK);
      this->display->drawLine(x1 + i, y3, x2 + i, y2, PINK);
    }
    this->display->drawCircle(55, 180, 22, RED);
    this->display->fillCircle(55, 180, 17, RED);
    this->display->setTextSize(2);
    this->display->setTextColor(RED);
    bheadershow = false; // header has been showed
  }
}

void displayCLD::preHeat80CLD()
{
  if (_bottomThermometer.getNewTemperatureScreenFlag()) // if there is temperature data to display
  {
    this->display->fillRect(90, 160, 180, 60, BLACK);
    this->display->setCursor(90, 190);
    double *bottomTemperature = _PIDControl.getBottomTemperature();
    // double *hotlidTemperature = _PIDControl.getHotlidTemperature();
    this->display->printf("%d", int(bottomTemperature[0])); // show the tempeature of heater1 and hotlid1
    _bottomThermometer.clearNewTemperatureScreenFlag();     // clear the flag after display the temperature
  }
}

void displayCLD::waitLysisTube()
{
  unsigned long now = millis();
  if (timeRefresh > now) // no refresh needed
  {
    return;
  }
  timeRefresh = now + 10 * 1000; // refresh every 10 seconds
  if (language == 0)
  {
  }
  else
  {
    this->display->fillScreen(BLACK);
    this->display->setTextSize(2);
    this->display->setTextColor(Forte_Green);
    this->display->setCursor(15, 60);
    this->display->print("Put the lysis tube");
    this->display->setCursor(15, 90);
    this->display->print("and close the lid");
    this->display->drawRect(30, 140, 272, 80, RED);
    this->display->drawRect(29, 139, 274, 82, RED);
    for (int i = 18; i <= 310; i += 10)
    {
      static int x1 = 0, y1 = 100, x2 = 10, y2 = 110, y3 = 120;
      this->display->drawLine(x1 + i, y1, x2 + i, y2, PINK);
      this->display->drawLine(x1 + i, y3, x2 + i, y2, PINK);
    }
    this->display->drawCircle(55, 180, 22, RED);
    this->display->fillCircle(55, 180, 17, RED);
    this->display->setTextSize(2);
    this->display->setTextColor(RED);
    this->display->setCursor(90, 175);
    // this->display->println("Press red");
    // this->display->setCursor(90, 205);
    // this->display->print("to measure #");
    // this->display->print(this->couter);
    this->display->println("Press Red to");
    this->display->setCursor(90, 205);
    this->display->print("Start Lysis");
    // this->display->print(this->couter);
  }
}

void displayCLD::waitLysis10min()
{
  unsigned long now = millis();
  if (timer10minEnd < now) // if reached 10mins
  {
    type_infor = ewaitphase2; // change status to next step
    changeScreen = true;
    _buzzer.BuzzerAlert();
    return;
  }
  if (timeRefresh > now) // no refresh needed
  {
    return;
  }
  timeRefresh = now + 1 * 1000; // refresh every second
  if (bheadershow)
  {
    if (language == 0)
    {
    }
    else
    {
      this->display->fillScreen(BLACK);
      // this->display->drawRoundRect(15, 0, 302, 240, 10, Forte_Green);
      this->display->drawBitmap(18, 5, logoFBT, 35, 34, Forte_Green);
      this->display->setTextSize(3);
      this->display->setTextColor(RED);
      this->display->setCursor(18, 90);
      this->display->print("Heating...");
      this->display->setTextSize(2);
      this->display->setTextColor(Forte_Green);
      this->display->setCursor(18, 150);
      this->display->println("Time left: ");
    }
    bheadershow = false;
  }
  this->display->fillRect(18, 150, 320, 90, BLACK);
  this->display->setCursor(90, 190);
  unsigned long timeleft = (timer10minEnd - now) / 1000; // seconds left
  // this->display->printf("%d minute", timeleft / (60), timeleft % 60); // show the time left
  this->display->printf("%d minute", ((timeleft / (60)) + 1)); // show the time left
  // this->display->printf("%d minute", timeleft / (60)); // show the time left
}

void displayCLD::startHeating10mins()
{
  timer10minEnd = millis() + LYSIS_DURATION * 1000; // calculate the end time of the 10mins
  bheadershow = true;                               // use it again to show header of the display
  timeRefresh = 0;
}

void displayCLD::waitBtnStartPhase2() // can add more buzzer alert in the future
{
  unsigned long now = millis();
  if (timeRefresh > now) // no refresh needed
  {
    return;
  }
  timeRefresh = now + 10 * 1000; // refresh every 10 seconds
  if (language == 0)
  {
  }
  else
  {
    this->display->fillScreen(BLACK);
    this->display->setTextSize(2);
    this->display->setTextColor(Forte_Green);
    this->display->setCursor(15, 60);
    this->display->print("Take the lysis tube");
    this->display->setCursor(15, 90);
    this->display->print("then close the lid");
    this->display->drawRect(30, 140, 272, 80, GREEN);
    this->display->drawRect(29, 139, 274, 82, GREEN);
    for (int i = 18; i <= 310; i += 10)
    {
      static int x1 = 0, y1 = 100, x2 = 10, y2 = 110, y3 = 120;
      this->display->drawLine(x1 + i, y1, x2 + i, y2, PINK);
      this->display->drawLine(x1 + i, y3, x2 + i, y2, PINK);
    }
    this->display->drawCircle(55, 180, 22, GREEN);
    this->display->fillCircle(55, 180, 17, GREEN);
    this->display->setTextSize(2);
    this->display->setTextColor(GREEN);
    this->display->setCursor(90, 175);
    this->display->println("Press Green");
    this->display->setCursor(90, 205);
    this->display->print("to preheat 67");
  }
}

void displayCLD::waitAmpTube()
{
  unsigned long now = millis();
  if (timeRefresh > now) // no refresh needed
  {
    return;
  }
  timeRefresh = now + 10 * 1000; // refresh every 10 seconds
  if (language == 0)
  {
  }
  else
  {
    this->display->fillScreen(BLACK);
    this->display->setTextSize(2);
    this->display->setTextColor(Forte_Green);
    this->display->setCursor(15, 60);
    this->display->print("Put the Amp tube");
    this->display->setCursor(15, 90);
    this->display->print("and close the lid");
    this->display->drawRect(30, 140, 272, 80, RED);
    this->display->drawRect(29, 139, 274, 82, RED);
    for (int i = 18; i <= 310; i += 10)
    {
      static int x1 = 0, y1 = 100, x2 = 10, y2 = 110, y3 = 120;
      this->display->drawLine(x1 + i, y1, x2 + i, y2, PINK);
      this->display->drawLine(x1 + i, y3, x2 + i, y2, PINK);
    }
    this->display->drawCircle(55, 180, 22, RED);
    this->display->fillCircle(55, 180, 17, RED);
    this->display->setTextSize(2);
    this->display->setTextColor(RED);
    this->display->setCursor(90, 175);
    this->display->println("Press Red to");
    this->display->setCursor(90, 205);
    this->display->print("Measure");
    // this->display->setTextColor(WHITE);
    // this->display->setTextSize(1);
    // this->display->setCursor(30, 20);
    // this->display->print("Kit ID: ");
    // this->display->print(String(_ForteSetting.parameter.kitId));
  }
}

void displayCLD::startAmplification()
{
  timer30minEnd = millis() + AMPLIFICATION_DURATION; // * 1000;//_sensor6035.getOPTO_DURATION();//AMPLIFICATION_DURATION*60*1000;   //calculate the end time of the 10mins
  bheadershow = true;                                // use it again to show header of the display
  timeRefresh = 0;
}

void displayCLD::waitAmplification30min()
{
  unsigned long now = millis();
  if (timer30minEnd < now) // if reached 30mins
  {
    //   type_infor = ewaitphase2; //change status to next step
    //   _buzzer.BuzzerAlert();
    return;
  }
  if (timeRefresh > now) // no refresh needed
  {
    return;
  }
  timeRefresh = now + 1 * 1000; // refresh every second
  if (bheadershow)
  {
    _sensor6035.setStepeSensorstart();
    if (language == 0)
    {
    }
    else
    {
      this->display->fillScreen(BLACK);
      // this->display->drawRoundRect(15, 0, 302, 240, 10, Forte_Green);
      this->display->drawBitmap(18, 5, logoFBT, 35, 34, Forte_Green);
      this->display->setTextSize(2);
      this->display->setTextColor(RED);
      this->display->setCursor(18, 90);
      this->display->print("Amplification..");
      this->display->setTextSize(2);
      this->display->setTextColor(Forte_Green);
      this->display->setCursor(18, 150);
      this->display->println("Time left: ");
    }
    bheadershow = false;
  }
  this->display->fillRect(18, 150, 320, 90, BLACK);
  this->display->setCursor(90, 190);
  unsigned long timeleft = (timer30minEnd - now) / 1000;     // seconds left
  this->display->printf("%d Minute", (timeleft / (60) + 1)); // show the time left
}

void displayCLD::prepare()
{
  unsigned long now = millis();
  if (timeRefresh > now) // no refresh needed
  {
    return;
  }
  timeRefresh = now + 10 * 1000; // refresh every 10 seconds
  if (language == 0)
  {
    this->display->fillScreen(BLACK);
    this->display->setTextSize(2);
    // this->display->drawRoundRect(15, 0, 302, 240, 10, Forte_Green);
    this->display->setTextColor(Forte_Green);
    this->display->setCursor(25, 60);
    this->display->println("Đặt ống vào máy");
    this->display->setCursor(25, 90);
    this->display->print("và đậy nắp");
    // this->display->drawRect(0, 0, 320, 240, Forte_Green);
    // this->display->drawRect(185, 0, 135, 35, Forte_Green);
    this->display->drawRect(30, 140, 272, 80, RED);
    this->display->drawRect(29, 139, 274, 82, RED);
    for (int i = 18; i <= 300; i += 10)
    {
      static int x1 = 0, y1 = 100, x2 = 10, y2 = 110, y3 = 120;
      this->display->drawLine(x1 + i, y1, x2 + i, y2, PINK);
      this->display->drawLine(x1 + i, y3, x2 + i, y2, PINK);
    }
    this->display->drawCircle(55, 180, 22, RED);
    this->display->fillCircle(55, 180, 17, RED);
    this->display->setTextSize(2);
    this->display->setTextColor(RED);
    this->display->setCursor(90, 175);
    this->display->println("Nút đỏ");
    this->display->setCursor(90, 205);
    this->display->print("để đo lần ");
    this->display->print(this->couter);
  }
  else
  {
    this->display->fillScreen(BLACK);
    this->display->setTextSize(2);
    this->display->setTextColor(Forte_Green);
    this->display->setCursor(15, 60);
    this->display->print("Put the tube inside");
    this->display->setCursor(15, 90);
    this->display->print("and close the lid");
    this->display->drawRect(30, 140, 272, 80, RED);
    this->display->drawRect(29, 139, 274, 82, RED);
    for (int i = 18; i <= 310; i += 10)
    {
      static int x1 = 0, y1 = 100, x2 = 10, y2 = 110, y3 = 120;
      this->display->drawLine(x1 + i, y1, x2 + i, y2, PINK);
      this->display->drawLine(x1 + i, y3, x2 + i, y2, PINK);
    }
    this->display->drawCircle(55, 180, 22, RED);
    this->display->fillCircle(55, 180, 17, RED);
    this->display->setTextSize(2);
    this->display->setTextColor(RED);
    this->display->setCursor(90, 175);
    this->display->println("Press Red to");
    this->display->setCursor(90, 205);
    this->display->print("measure");
  }
}

void displayCLD::screen_Result(char key)
{
  {
    float CT_value[10] = {0};
    char result[10] = {0};
    uint8_t loops = _ForteSetting.parameter.amplification_time;
    SerialBT.end();

    getDataAmplificationEEPROM();

    info_displayln("<AmpStart/>");
    _sensor6035.outputHeader();
    for (size_t i = 0; i < loops; i++) // cnt
    {
      info_display(float(i) * OPTO_INTERVAL / 60000.0); // time
      info_display(",");
      for (size_t j = 0; j < 10; j++) // LED channel
      {
        info_display(_sensor6035.calCalibratedValue(j, i));
        info_display(",");
        delay(1);
      }
      info_displayln(_ForteSetting.parameter.amplifTemp);
    }

    /* Kiểm tra Wifi trước khi tính toán kết quả và gửi lên Sheet */
    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 50)
    {
      delay(100);
      retries++;
      Serial.print(".");
      WiFi.begin(ssid.c_str(), password.c_str());
    }

    if ((WiFi.status() == WL_CONNECTED) && (key == 'f'))
    {
      /* Xuất kết quả lên google Sheet khi có wifi */
      postData_GoogleSheet(CT_value, result, loops);
    }
    else
    {
      bool flag = _sensor6035.bResultGet(CT_value, result);
    }

    this->display->fillScreen(BLACK);
    this->display->setTextSize(2);

    // display the block number
    this->display->setTextColor(WHITE);
    this->display->setCursor(100, 30); // start position of each sensor value
    this->display->printf("L");
    this->display->setCursor(215, 30); // start position of each sensor value
    this->display->printf("R");
    // display the list
    this->display->setTextColor(WHITE);
    for (u8_t i = 0; i < (OPTOCHANNELS / 2); i++)
    {
      this->display->setCursor(15, 70 + 35 * (i % 5)); // start position of each channel name
      this->display->printf("%02d", (5 - i));
      this->display->setCursor(280, 70 + 35 * (i % 5)); // start position of each channel name
      this->display->printf("%02d", (10 - i));
    }

    for (u8_t i = 0; i < OPTOCHANNELS; i++)
    {
      this->display->setCursor(55 + 120 * (i / 5), 70 + 35 * ((OPTOCHANNELS - i - 1) % 5)); // start position of each sensor value

      if (result[i] == 'N')
      {
        this->display->setTextColor(Forte_Green);
        this->display->printf("|----|");
      }
      else if (result[i] == 'S')
      {
        this->display->setTextColor(YELLOW);
        this->display->printf("|%04.01f|", CT_value[i]);
      }
      else if (result[i] == 'P')
      {
        this->display->setTextColor(RED);
        this->display->printf("|%04.01f|", CT_value[i]);
      }
      else if (result[i] == 'E')
      {
        this->display->setTextColor(ORANGE);
        this->display->printf("|  ! |", CT_value[i]);
      }
      else if (result[i] == 'B')
      {
        this->display->setTextColor(CYAN);
        this->display->printf("| ---|", CT_value[i]);
            }
    }

    this->display->setTextColor(WHITE);
    this->display->setTextSize(1);
    this->display->setCursor(15, 230);
    this->display->printf("Press white key to test next");

    changeScreen = false;
  }
}

void displayCLD::set_connect_bluetooth()
{
  if (language == 0)
  {
    this->display->fillScreen(BLACK);
    // this->display->drawRoundRect(15, 0, 302, 240, 10, Forte_Green);
    this->display->setTextSize(2);
    this->display->setTextColor(ORANGE);
    this->display->setCursor(70, 30);
    this->display->print("Cài đặt WIFI");
    this->display->drawRect(18, 50, 296, 50, WHITE);
    this->display->setTextColor(WHITE);
    this->display->setTextSize(1);
    this->display->setCursor(25, 90);
    this->display->print("WiFi ID: " + ssid);
    this->display->drawRect(18, 110, 296, 50, WHITE);
    this->display->setCursor(25, 150);
    this->display->print("Password: " + password);
    this->display->drawRect(18, 170, 296, 50, WHITE);
    this->display->setCursor(25, 210);
    this->display->print("ID: " + id);
    connectWIFI();       // Obtain Wifi ID and password from user via bluetooth
    saveSettingDevice(); // save Wifi ID and password in EEPROM
    loadSettingDevice();
    this->display->fillScreen(BLACK);
    this->display->setTextSize(2);
    this->display->setCursor(25, 120);
    this->display->setTextColor(GREEN);
    this->display->print("Cài đặt thành công!");
    delay(2000);
    this->display->fillScreen(BLACK);
    // this->display->drawRoundRect(15, 0, 302, 240, 10, Forte_Green);
    this->display->setTextSize(2);
    this->display->setTextColor(ORANGE);
    this->display->setCursor(70, 30);
    this->display->print("Cài đặt  WIFI");
    this->display->drawRect(18, 50, 296, 50, WHITE);
    this->display->setTextColor(WHITE);
    this->display->setTextSize(1);
    this->display->setCursor(25, 90);
    this->display->print("WiFi ID: " + ssid);
    this->display->drawRect(18, 110, 296, 50, WHITE);
    this->display->setCursor(25, 150);
    this->display->print("Password: " + password);
    this->display->drawRect(18, 170, 296, 50, WHITE);
    this->display->setCursor(25, 200);
    this->display->print("ID: " + id);
    delay(2000);
    ESP.restart();
  }
  else
  {
    this->display->fillScreen(BLACK);
    // this->display->drawRect(0,0,320,240,BLUE);
    // this->display->drawRoundRect(15, 0, 302, 240, 10, Forte_Green);
    this->display->setTextSize(2);
    this->display->setTextColor(ORANGE);
    this->display->setCursor(70, 30);
    this->display->print("WIFI Set up");
    this->display->drawRect(18, 50, 296, 50, WHITE);
    this->display->setTextColor(WHITE);
    this->display->setTextSize(1);
    this->display->setCursor(25, 90);
    this->display->print("WiFi ID: " + ssid);
    this->display->drawRect(18, 110, 296, 50, WHITE);
    this->display->setCursor(25, 150);
    this->display->print("Password: " + password);
    this->display->drawRect(18, 170, 296, 50, WHITE);
    this->display->setCursor(25, 210);
    this->display->print("ID: " + id);
    connectWIFI();       // Obtain Wifi ID and password from user via bluetooth
    saveSettingDevice(); // save Wifi ID and password in EEPROM
    loadSettingDevice();
    // Update newScreen
    this->display->fillScreen(BLACK);
    this->display->setTextSize(2);
    this->display->setCursor(25, 120);
    this->display->setTextColor(GREEN);
    this->display->print("Succesfull!");
    delay(2000);
    this->display->fillScreen(BLACK);
    // this->display->drawRoundRect(15, 0, 302, 240, 10, Forte_Green);
    this->display->setTextSize(2);
    this->display->setTextColor(ORANGE);
    this->display->setCursor(70, 30);
    this->display->print("WIFI Set up");
    this->display->drawRect(18, 50, 296, 50, WHITE);
    this->display->setTextColor(WHITE);
    this->display->setTextSize(1);
    this->display->setCursor(25, 90);
    this->display->print("WiFi ID: " + ssid);
    this->display->drawRect(18, 110, 296, 50, WHITE);
    this->display->setCursor(25, 150);
    this->display->print("Password: " + password);
    this->display->drawRect(18, 170, 296, 50, WHITE);
    this->display->setCursor(25, 200);
    this->display->print("ID: " + id);
    delay(2000);
    ESP.restart();
  }
}

void settingSucces(String title)
{
  _displayCLD.display->fillScreen(BLACK);
  _displayCLD.display->setTextSize(2);
  _displayCLD.display->setCursor(25, 120);
  _displayCLD.display->setTextColor(GREEN);
  _displayCLD.display->print(title);
  delay(1000);
  // ESP.restart();
}

void displayCLD::loop()
{
  if (this->changeScreen)
  {
    switch (this->type_infor)
    {
    case escreenStart:
    {
      // Serial.println("Truoc khi BT ket noi: " + String(ESP.getFreeHeap()));
      // connectBLE();
      // Serial.println("Sau khi BT ket noi: " + String(ESP.getFreeHeap()));
      dbg_display("escreenStart");
      this->screen_Start();
      // _sensor6035.clear();
      this->changeScreen = false;
      break;
    }
    case epreheating80:
      dbg_display("epreheating80");
      this->preHeat80CLD_Header();
      this->preHeat80CLD();
      break;
    case ewaitLysisTube: // wait user to put lysis tube and start
      waitLysisTube();
      // this->prepare();
      break;
    case eheatLysis:
      waitLysis10min();
      break;
    case ewaitphase2:
      waitBtnStartPhase2();
      break;

    case epreheating67:
    {
      dbg_display("epreheating67");
      this->preHeat67CLD_Header();
      this->preHeat67CLD();
      break;
    }

    case ewaitampTube:
    {
      waitAmpTube();
      break;
    }

      // case eincreaseto80:
      // {
      //   dbg_display("eincreaseto80");
      //   this->preHeat80CLD_Header();
      //   this->preHeat80CLD();
      //   break;
      // }
    case eoptoreading:
    case ewaitingReadsensor:
    {
      dbg_display("ewaitingReadsensor");
      waitAmplification30min();
      // waiting_Readsensor();
      break;
    }
    case eprepare:
    {
      dbg_display("eprepare");
      this->prepare();
      break;
    }

    case escreenResult:
    {
      dbg_display("escreenResult lan %d", this->couter);
      // this->screen_Result();
      break;
    }
    case escreenFinished:
    {
      _PIDControl.rerun();
      this->screen_Result('f');
      break;
    }
    case escreenReview:
    {
      settingSucces("Waiting......!");
      this->screen_Result('r');
      break;
    }
    case errprocess:
    {
      // this->NextTestDisplay();
      // this->RestartProcess("Reboot after Err", "Rebooting...");
      break;
    }
    // case eErrResart:
    // {
    //   this->RestartProcess("Reboot after Err", "Rebooting...");
    //   break;
    // }
    case escreenRestart:
    {
      // this->ErrRebootDisplay();
      this->RestartProcess("To test next one", "Rebooting...");
      break;
    }
    case ebuttonrestart:
    {
      this->RestartProcess("Restarted by user", "Rebooting...");
      break;
    }
    case ewaitingtimeout:
    {
      if (millis() > timeRefresh)
      {
        _ForteSetting.rerun();
        type_infor = escreenStart;
        _buzzer.BuzzerStop();
      }
      break;
    }
    case eSettingMenu:
    {
      this->setting_Menu();
      this->changeScreen = false;
      break;
    }
    case eSettingWifi:
    {
      this->setting_Wifi();
      this->changeScreen = false;
      break;
    }
    case eUpLoadData:
    {
      displayWaitingUpData();
      // postData_GoogleSheet();
      this->screen_Result('f');
      // settingSucces("Up Data Success!");
      // this->type_infor = escreenStart;
      // this->changeScreen = true;
      break;
    }
    case eSettingBluetooth:
    {
      connectBLE();
      // settingSucces("Settings Bluetoot Success!");
      ESP.restart();
      break;
    }
    case eSelectAmpli:
    {
      this->display_Select_menu_calib();
      this->changeScreen = false;
      break;
    }
    case eSelectMode:
    {
      this->display_Select_mode();
      this->changeScreen = false;
      break;
    }
    case eSelectSlot:
    {
      this->display_Select_slot();
      this->changeScreen = false;
      break;
    }
    case eCalibrating:
    {
      this->display_Calib();
      this->changeScreen = false;
      break;
    }
    case eWaitingCalib:
    {
      this->display_Waiting_Calib();
      this->changeScreen = false;
      break;
    }
    case eCalibComplete:
    {
      this->display_Calib_Complete();
      if (!flag_calib_done)
      {
        this->changeScreen = false;
      }
      break;
    }
    case eSetPowerLed:
    {
      this->display_Set_powerled();
      this->changeScreen = false;
      break;
    }
    case eSavePowerLed:
    {
      this->calculate();
      this->changeScreen = false;
      break;
    }
    case eSaveCalib:
    {
      this->saving_calib();
      this->changeScreen = false;
      break;
    }
    case eUpdateOTA:
    {
      this->display_UpdateOTA();
      this->changeScreen = false;
      break;
    }

    default:
      break;
    }

    // EEPROM.end();
    show_IconWifi();
    // show_IconBluetooth();
    // this->changeScreen = false;
  }
}

void displayCLD::rerun()
{
  // type_infor = escreenStart;
  changeScreen = true;
  // info_displayf("status of LCD is %d\n", type_infor);
}

void displayCLD::setting_Menu(void)
{
  info_display("setting menu\n");
  this->display->fillScreen(BLACK);

  this->display->fillRect(108, 0, 108, 20, Forte_Green);
  // this->display->drawRoundRect(15, 0, 302, 240, 10, Forte_Green);
  this->display->drawBitmap(18, 5, logoFBT, 35, 34, Forte_Green);
  this->display->setTextSize(1);
  this->display->setCursor(110, 15);
  this->display->setTextColor(BLACK);
  this->display->println("FORTE BIOTECH");

  this->display->setTextWrap(false);
  this->display->setTextSize(2);

  this->display->drawRoundRect(30, 78, 272, 50, 10, GREEN);
  this->display->drawCircle(50, 100, 16, GREEN);
  this->display->fillCircle(50, 100, 12, GREEN);
  this->display->setTextColor(GREEN);
  this->display->setCursor(70, 110);
  this->display->print("Wifi/Update");

  this->display->drawRoundRect(30, 130, 272, 50, 10, RED);
  this->display->drawCircle(50, 155, 16, RED);
  this->display->fillCircle(50, 155, 12, RED);
  this->display->setTextColor(RED);
  this->display->setCursor(70, 165);
  this->display->print("Up Data");

  this->display->drawRoundRect(30, 185, 272, 50, 10, WHITE);
  this->display->drawCircle(50, 210, 16, WHITE);
  this->display->fillCircle(50, 210, 12, WHITE);
  this->display->setTextColor(WHITE);
  this->display->setCursor(70, 220);
  this->display->print("Bluetooth");

  this->display->setTextSize(2);
  this->display->setTextColor(WHITE);
  this->display->setCursor(45, 55);
  this->display->print("RAPID Settings");
}

void displayCLD::setting_Wifi(void)
{
  if (WiFi.status() == WL_DISCONNECTED)
  {
    password = " ";
    ssid = " ";
  }
  if (id_device == "")
  {
    id_device = " ";
  }

  {
    this->display->fillScreen(BLACK);
    this->display->fillRect(108, 0, 108, 20, Forte_Green);
    this->display->drawBitmap(18, 5, logoFBT, 35, 34, Forte_Green);
    this->display->setTextSize(1);
    this->display->setCursor(110, 15);
    this->display->setTextColor(BLACK);
    this->display->println("FORTE BIOTECH");
    this->display->setTextWrap(false);
    this->display->setTextSize(2);
    this->display->setCursor(45, 55);
    this->display->print("Settings");
    this->display->setCursor(45, 85);
    this->display->print("Device/Update");

    this->display->setTextSize(1);
    this->display->drawRoundRect(29, 106, 272, 30, 10, RED);
    this->display->setTextColor(WHITE);
    this->display->setCursor(35, 180);
    this->display->print("wifi name:");
    this->display->print(String(ssid));
    this->display->setCursor(35, 200);
    this->display->print("password :");
    this->display->print(String(password));
    this->display->drawRoundRect(30, 158, 272, 60, 10, GREEN);
    this->display->setCursor(35, 123);
    this->display->print("id device:");
    this->display->print(String(id_device));
  }
  Wifi_Connect();
  {
    this->display->fillScreen(BLACK);
    this->display->fillRect(108, 0, 108, 20, Forte_Green);
    // this->display->drawRoundRect(15, 0, 302, 240, 10, Forte_Green);
    this->display->drawBitmap(18, 5, logoFBT, 35, 34, Forte_Green);

    this->display->setTextSize(2);
    this->display->setCursor(45, 55);
    this->display->print("Settings");
    this->display->setCursor(45, 85);
    this->display->print("Device/Update");

    this->display->setTextWrap(false);
    this->display->setTextSize(1);
    this->display->drawRoundRect(29, 106, 272, 30, 10, RED);
    this->display->setTextColor(WHITE);
    this->display->setCursor(35, 180);
    this->display->print("wifi name:");
    this->display->print(String(ssid));
    this->display->setCursor(35, 200);
    this->display->print("password :");
    this->display->print(String(password));
    this->display->drawRoundRect(30, 158, 272, 60, 10, GREEN);
    this->display->setCursor(35, 123);
    this->display->print("id device:");
    this->display->print(String(id_device));
  }
  delay(1000);
  esp_restart();
}

void displayCLD::setting_Language(void)
{
}

/* Function Calib */
void displayCLD::display_Select_menu_calib(void)
{
  this->display->fillScreen(BLACK);
  this->display->drawRoundRect(8, 0, 305, 240, 10, Forte_Green);
  this->display->drawRoundRect(8, 40, 305, 170, 0, Forte_Green);

  this->display->setTextColor(WHITE);
  this->display->setTextSize(2);
  this->display->setCursor(60, 30);
  this->display->println("Select Mode");

  this->display->setTextSize(2);
  this->display->setTextColor(GREEN);
  this->display->setCursor(40, 100);
  this->display->print("Calibration");
  this->display->setTextColor(RED);
  this->display->setCursor(40, 140);
  this->display->print("Amplification");
  this->display->setCursor(40, 170);
  this->display->print("Tube 0");

  this->display->setTextSize(1);
  this->display->setTextColor(WHITE);
  this->display->setCursor(275, 230);
  this->display->println("Back");
}
void displayCLD::display_Select_mode(void)
{
  this->display->fillScreen(BLACK);
  this->display->drawRoundRect(8, 0, 305, 240, 10, Forte_Green);
  this->display->drawRoundRect(8, 40, 305, 170, 0, Forte_Green);

  this->display->setTextColor(WHITE);
  this->display->setTextSize(2);
  this->display->setCursor(60, 30);
  this->display->println("Select Mode");

  this->display->setTextSize(2);
  this->display->setTextColor(GREEN);
  this->display->setCursor(40, 100);
  this->display->print("Calibration");
  this->display->setTextColor(RED);
  this->display->setCursor(40, 140);
  this->display->print("Setting LED power");

  this->display->setTextSize(1);
  this->display->setTextColor(WHITE);
  this->display->setCursor(275, 230);
  this->display->println("Back");
}

void displayCLD::display_Select_slot(void)
{
  this->display->fillScreen(BLACK);
  this->display->drawRoundRect(8, 0, 305, 240, 10, Forte_Green);
  this->display->drawRoundRect(8, 40, 305, 170, 0, Forte_Green);

  this->display->setTextSize(2);
  this->display->setTextColor(WHITE);
  this->display->setCursor(65, 30);
  this->display->print("Calibration");

  this->display->setCursor(60, 100);
  this->display->print("Select slot:");
  this->display->print(this->slot + 1);
  this->display->setTextSize(1);
  this->display->setTextColor(YELLOW);
  this->display->setCursor(60, 140);
  this->display->print("Slope:");
  this->display->println(_ForteSetting.parameter.slopes[slot]);
  this->display->setCursor(60, 160);
  this->display->print("Origin:");
  this->display->println(_ForteSetting.parameter.origins[slot]);
  this->display->setCursor(60, 180);
  this->display->print("LED power:");
  this->display->println(_ForteSetting.parameter.led_power[slot]);

  this->display->setTextColor(GREEN);
  this->display->setCursor(20, 230);
  this->display->print("Select");
  this->display->setTextColor(RED);
  this->display->setCursor(150, 230);
  this->display->print("Next");
  this->display->setTextColor(WHITE);
  this->display->setCursor(275, 230);
  this->display->print("Exit");
}

void displayCLD::display_Calib(void)
{
  this->display->fillScreen(BLACK);
  this->display->drawRoundRect(8, 0, 305, 240, 10, Forte_Green);

  this->display->setTextSize(2);
  this->display->setTextColor(WHITE);
  this->display->setCursor(30, 100);
  this->display->print("Put the tube ");

  switch (_sensor6035.type_calib)
  {
  case 0:
    this->display->print("300");
    break;
  case 1:
    this->display->print("200");
    break;
  case 2:
    this->display->print("100");
    break;
  case 3:
    this->display->print("0");
    break;
  default:
    break;
  }

  this->display->setCursor(80, 130);
  this->display->print("into slot ");
  this->display->print(this->slot + 1);

  this->display->setTextSize(1);
  this->display->setTextColor(GREEN);
  this->display->setCursor(20, 230);
  this->display->print("Calib");
}

void displayCLD::display_Waiting_Calib(void)
{
  this->display->fillScreen(BLACK);
  this->display->drawRoundRect(8, 0, 305, 240, 10, Forte_Green);

  this->display->setTextSize(2);
  this->display->setTextColor(RED);
  this->display->setCursor(20, 90);
  this->display->print("Calibrating");
  this->display->setTextSize(2);
  this->display->setTextColor(Forte_Green);
  this->display->setCursor(40, 150);
  this->display->println("Waitting...");
}

void displayCLD::display_Calib_Complete(void)
{
  this->display->fillScreen(BLACK);
  this->display->drawRoundRect(8, 0, 305, 240, 10, Forte_Green);

  this->display->setTextSize(2);
  this->display->setTextColor(WHITE);
  this->display->setCursor(50, 120);
  this->display->print("Slope:  ");
  if (_sensor6035.cal_calib[0] < 0.5 | _sensor6035.cal_calib[0] > 3.5)
  {
    this->display->setTextColor(RED);
  }
  this->display->println(_sensor6035.cal_calib[0], 3); // slope
  this->display->setTextColor(WHITE);
  this->display->setCursor(50, 150);
  this->display->print("RSQ:    ");
  if (_sensor6035.cal_calib[1] < 0.95)
  {
    this->display->setTextColor(RED);
  }
  this->display->println(_sensor6035.cal_calib[1], 3); // RSQ
  this->display->setTextColor(WHITE);
  this->display->setCursor(50, 180);
  this->display->print("Origin: ");
  this->display->println(_sensor6035.cal_calib[2], 1); // origin

  if ((_sensor6035.cal_calib[0] < 0.5) | (_sensor6035.cal_calib[0] > 3.5) | (_sensor6035.cal_calib[1] < 0.95))
  {
    this->display->setTextSize(2);
    this->display->setTextColor(RED);
    this->display->setCursor(25, 60);
    this->display->print("Failed Calib!");
    this->display->setTextSize(1);
    this->display->setCursor(140, 230);
    this->display->print("Setting LED");
    this->display->setTextColor(GREEN);
    this->display->setCursor(20, 230);
    this->display->print("Calib again");
  }
  else
  {
    flag_calib_done = true;
    this->display->setTextSize(2);
    this->display->setTextColor(GREEN);
    this->display->setCursor(25, 60);
    this->display->print("Done Calib!");
    set_flag_calib();
  }
}

void displayCLD::display_Set_powerled(void)
{
  this->display->fillScreen(BLACK);
  this->display->drawRoundRect(8, 0, 305, 240, 10, Forte_Green);
  this->display->drawRoundRect(8, 40, 305, 170, 0, Forte_Green);

  this->display->setTextSize(2);
  this->display->setTextColor(WHITE);
  this->display->setCursor(65, 30);
  this->display->println("Setting LED");

  this->display->setTextSize(5);
  this->display->setCursor(105, 140);
  this->display->println(this->led_power[0]);
  this->display->setCursor(145, 140);
  this->display->println(this->led_power[1]);
  this->display->setCursor(185, 140);
  this->display->println(this->led_power[2]);

  switch (this->index)
  {
  case 0:
  {
    this->display->fillTriangle(110, 65, 140, 65, 125, 85, WHITE);
    break;
  }
  case 1:
  {
    this->display->fillTriangle(150, 65, 180, 65, 165, 85, WHITE);
    break;
  }
  case 2:
  {
    this->display->fillTriangle(190, 65, 220, 65, 205, 85, WHITE);
    break;
  }
  default:
    break;
  }

  this->display->setTextSize(1);
  this->display->setCursor(20, 230);
  this->display->setTextColor(GREEN);
  this->display->println("Next");
  this->display->setCursor(150, 230);
  this->display->setTextColor(RED);
  this->display->println("Up");
  this->display->setCursor(275, 230);
  this->display->setTextColor(WHITE);
  this->display->println("Save");
}
void displayCLD::calculate(void)
{
  this->display->fillScreen(BLACK);
  this->display->drawRoundRect(15, 0, 302, 240, 10, Forte_Green);

  this->display->setTextSize(2);
  this->display->setTextColor(WHITE);
  this->display->setCursor(40, 90);
  this->display->print("Saved LED power!");

  int tmp = 0;
  for (int i = 0; i < 3; i++)
  {
    tmp = tmp * 10 + this->led_power[i];
  }
  _ForteSetting.parameter.led_power[this->slot] = tmp;
  EEPROM.begin(_EEPROM_SIZE);
  EEPROM.put(PARAMETERPOS, _ForteSetting.parameter);
  EEPROM.commit();
  EEPROM.end();

  this->display->setTextSize(1);
  this->display->setTextColor(GREEN);
  this->display->setCursor(20, 230);
  this->display->print("Next");
}

void displayCLD::saving_calib(void)
{
  this->display->fillScreen(BLACK);
  this->display->drawRoundRect(8, 0, 305, 240, 10, Forte_Green);

  this->display->setTextSize(2);
  this->display->setTextColor(WHITE);
  this->display->setCursor(40, 90);
  this->display->print("Saved calibration!");

  _ForteSetting.parameter.slopes[this->slot] = _sensor6035.cal_calib[0];
  EEPROM.begin(_EEPROM_SIZE);
  EEPROM.put(PARAMETERPOS, _ForteSetting.parameter);
  EEPROM.commit();
  EEPROM.end();

  this->display->setTextSize(1);
  this->display->setTextColor(GREEN);
  this->display->setCursor(20, 230);
  this->display->print("Next");
}

void displayCLD::set_flag_calib(void)
{
  delay(3000);
  _displayCLD.type_infor = eSaveCalib;
  _displayCLD.changeScreen = true;
}

void displayCLD::display_UpdateOTA(void)
{
  this->display->fillScreen(BLACK);
  this->display->drawRoundRect(8, 0, 305, 240, 10, Forte_Green);

  this->display->setTextSize(2);
  this->display->setTextColor(WHITE);
  this->display->setCursor(40, 60);
  this->display->println("You have a");
  this->display->setCursor(70, 90);
  this->display->println("new update!");

  this->display->setTextSize(1);
  this->display->setTextColor(WHITE);
  this->display->setCursor(40, 120);
  this->display->println("Version: " + fwVer);
  this->display->setCursor(40, 150);
  this->display->println("Detail: " + fwCont);

  this->display->setTextSize(1);
  this->display->setTextColor(RED);
  this->display->setCursor(40, 180);
  this->display->println("Press red button: Update");

  this->display->setTextSize(1);
  this->display->setTextColor(GREEN);
  this->display->setCursor(40, 210);
  this->display->println("Press green button: Skip");
}

void displayCLD::waittingUpdate(void)
{
  this->display->fillScreen(BLACK);
  this->display->drawRoundRect(8, 0, 305, 240, 10, Forte_Green);

  this->display->setTextSize(2);
  this->display->setTextColor(WHITE);
  this->display->setCursor(40, 100);
  this->display->print("Waitting...");
}

displayCLD _displayCLD;
