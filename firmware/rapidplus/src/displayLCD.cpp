#include "displayCLD.h"
#include <U8g2lib.h>
#include "displayresources.h"
#include "define.h"
#include "Adafruit_GFX.h"
#include "Adafruit_ILI9341.h"
// #include "sensor.h"
#include "sensor6035.h"
#include "errorCheck.h"
// #include "update_firmware.h"
#include "Bluetooth.h"
#include "PIDControl.h"
#include <string>
// #include "sensor6035.h"

#define Forte_Green 0x25F8
#define VIOLET 0xA81F
String measure_value = "";

// bool butt = 1;  // 0: blue, 1: green

/***********************************************************************
 * Function: displayCLD()
 * Description: Constructor for the displayCLD class. Allocates the
 *  Arduino_ESP32SPI bus object using the configured TFT SPI pins and
 *  creates the Arduino_ILI9341 display driver bound to that bus.
 * pramameter: none
 *  return: none
 */
displayCLD::displayCLD(/* args */)
{
  this->bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCK, TFT_MOSI, TFT_MISO);
  this->display = new Arduino_ILI9341(this->bus, TFT_RESET);
}

/***********************************************************************
 * Function: ~displayCLD()
 * Description: Destructor for the displayCLD class. Empty; performs no
 *  cleanup of the allocated bus/display objects.
 * pramameter: none
 *  return: none
 */
displayCLD::~displayCLD()
{
}

/***********************************************************************
 * Function: begin()
 * Description: Initializes the ILI9341 display: starts the driver,
 *  clears the screen to BLACK, sets landscape rotation (1) and enables
 *  UTF-8 printing so Vietnamese text can be rendered.
 * pramameter: none
 *  return: none
 */
void displayCLD::begin()
{
  this->display->begin();
  this->display->fillScreen(BLACK);
  this->display->setRotation(1);
  this->display->setUTF8Print(true);
}

/***********************************************************************
 * Function: logoFortebiotech()
 * Description: Draws the Forte Biotech splash/logo screen: clears to
 *  BLACK, renders the triangular logo shapes, the "FORTE BIOTECH"
 *  title, the tagline text and the firmware version, then blocks for
 *  LOGODISPLAYTIME before returning.
 * pramameter: none
 *  return: none
 */
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

/***********************************************************************
 * Function: show_IconWifi()
 * Description: Free function that draws the WiFi status icon at the top
 *  right of the screen: the connected bitmap when WiFi.status() is
 *  WL_CONNECTED, otherwise the disconnected bitmap.
 * pramameter: none
 *  return: none
 */
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

/***********************************************************************
 * Function: show_IconBluetooth()
 * Description: Free function that manages Bluetooth power state based on
 *  the current screen. While type_infor is escreenResult or escreenStart
 *  it marks BT as on; once the state leaves those screens it ends the
 *  SerialBT connection one time (turning Bluetooth off during a process).
 * pramameter: none
 *  return: none
 */
void show_IconBluetooth(void)
{
  static bool turnOn_BT = false; // turn off BT when Process is runing and save BT state to turn off once
  if (_displayCLD.type_infor == escreenResult || _displayCLD.type_infor == escreenStart)
  {
    turnOn_BT = true;
  }
  else
  {
    if (turnOn_BT == true)
    {
      turnOn_BT = false;
      /* turn off bluetooth*/
      SerialBT.end();
    }
  }
}
/***********************************************************************
 * Function: displayWaitingUpData()
 * Description: Free function that shows the "Data Uploading...!" /
 *  "Please wait..." screen while results are being uploaded, also draws
 *  the WiFi icon and adds a short 100ms delay.
 * pramameter: none
 *  return: none
 */
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

/***********************************************************************
 * Function: screen_Start()
 * Description: Draws the idle/start home screen. Reads the local IP and
 *  renders the "FORTE BIOTECH" header, logo and shrimp bitmaps. When
 *  language==0 it shows the Vietnamese "Nhấn nút xanh / để bắt đầu" prompt
 *  with the device ID; otherwise it shows the English prompt instructing
 *  Press Green for Lysis / Press Red for Amplification, plus IP and
 *  firmware version.
 * pramameter: none
 *  return: none
 */
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

/***********************************************************************
 * Function: ErrorProcessatBegin()
 * Description: Draws a startup error screen in RED with the provided
 *  description, a hazard-striped warning band, a bordered box and a
 *  filled circle, showing "Slot <strValue>" inside, then triggers the
 *  buzzer alarm. Used when an error is detected at the begin stage.
 * pramameter: strDescript - error description text shown at the top;
 *  strValue - slot identifier appended after the "Slot " label
 *  return: none
 */
void displayCLD::ErrorProcessatBegin(String strDescript, String strValue)
{
  this->display->fillScreen(BLACK);
  this->display->setTextSize(2);
  this->display->setTextColor(RED);
  this->display->setCursor(15, 60);
  this->display->print(strDescript);
  for (int i = 18; i <= 310; i += 10)
  {
    static int x1 = 0, y1 = 100, x2 = 10, y2 = 110, y3 = 120;
    this->display->drawLine(x1 + i, y1, x2 + i, y2, PINK);
    this->display->drawLine(x1 + i, y3, x2 + i, y2, PINK);
  }
  this->display->drawRect(30, 150, 272, 80, RED);
  this->display->drawRect(29, 149, 274, 82, RED);
  this->display->drawCircle(55, 190, 22, RED);
  this->display->fillCircle(55, 190, 17, RED);
  this->display->setTextSize(2);
  this->display->setTextColor(RED);

  this->display->setCursor(90, 200);
  this->display->print("Slot " + strValue); // show the tempeature of heater1 and hotlid1
  _buzzer.BuzzerAlarm();
}

/***********************************************************************
 * Function: ErrorDisplay()
 * Description: Clears the screen and prints the given error description
 *  in small WHITE text, sounds the buzzer alarm, then restores text size
 *  to 2. A minimal error message display.
 * pramameter: strDescript - the error description text to display
 *  return: none
 */
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

/***********************************************************************
 * Function: ErrorProcess()
 * Description: Draws the in-process error screen (only when type_infor
 *  is not already errprocess): RED description, hazard band, warning box
 *  and circle, the strValue, plus "Please restart power". Sounds the
 *  buzzer alarm, sets type_infor to errprocess and sets timeRefresh to
 *  display the error for ~1 second.
 * pramameter: strDescript - error description shown at top; strValue -
 *  value/text drawn inside the warning box
 *  return: none
 */
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
/***********************************************************************
 * Function: RestartProcess()
 * Description: Draws the restart/reboot screen: RED description, hazard
 *  band, warning box and circle with strValue, and a GREEN "Reboot in 1
 *  second" message. Sounds the buzzer alert, sets type_infor to
 *  ewaitingtimeout and schedules timeRefresh ~1 second ahead so the
 *  device reboots/returns to start afterwards.
 * pramameter: strDescript - description text at top; strValue - text
 *  drawn inside the warning box
 *  return: none
 */
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

/***********************************************************************
 * Function: ErrorStatus()
 * Description: Reports whether the display is currently in the error
 *  process state by comparing type_infor against errprocess.
 * pramameter: none
 *  return: bool - true if type_infor == errprocess, false otherwise
 */
bool displayCLD::ErrorStatus()
{
  return type_infor == errprocess; // return the status whether it's error process or not
}

/***********************************************************************
 * Function: FinishStatus()
 * Description: Reports whether the process has finished by comparing the
 *  global _displayCLD.type_infor against escreenFinished.
 * pramameter: none
 *  return: bool - true if type_infor == escreenFinished, false otherwise
 */
bool displayCLD::FinishStatus()
{
  return _displayCLD.type_infor == escreenFinished;
}

/***********************************************************************
 * Function: TemperatureBottomSeqDisplay()
 * Description: Diagnostic screen that shows the bottom temperature
 *  sensor sequence: title, the three live bottom sensor readings, the
 *  configured bottomTemperatureSensorSq order, and a prompt to press the
 *  white button to skip and simulate.
 * pramameter: none
 *  return: none
 */
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

/***********************************************************************
 * Function: TemperatureTopSeqDisplay()
 * Description: Diagnostic screen that shows the top (hotlid) temperature
 *  sensor sequence: title, the three live top sensor readings, the
 *  configured topTemperatureSensorSq order, and a prompt to press the
 *  white button to skip and simulate.
 * pramameter: none
 *  return: none
 */
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

/***********************************************************************
 * Function: NextTestDisplay()
 * Description: Shows a brief "Test next" message in RED, then sets
 *  type_infor to ewaitingtimeout and schedules timeRefresh ~1 second
 *  ahead so the device transitions afterwards.
 * pramameter: none
 *  return: none
 */
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

/***********************************************************************
 * Function: ErrRebootDisplay()
 * Description: Shows a brief "Restart testing" message in RED, schedules
 *  timeRefresh ~1 second ahead and sets type_infor to ewaitingtimeout so
 *  the device returns to the start screen after the error.
 * pramameter: none
 *  return: none
 */
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

/***********************************************************************
 * Function: drawHeat67Header()
 * Description: Shared static-header renderer for the two 67C/preheat screens
 *  (Heat67LCD_Header and Preheat67LCD_Header). Guarded by bheadershow: clears
 *  the screen, prints the two given title lines and the warning band/box, then
 *  clears bheadershow.
 * pramameter: line1, line2 - the two title text lines to show
 *  return: none
 */
void displayCLD::drawHeat67Header(const char *line1, const char *line2)
{
  if (bheadershow)
  {
    this->display->fillScreen(BLACK);
    this->display->setTextSize(2);
    this->display->setTextColor(Forte_Green);
    this->display->setCursor(15, 60);
    this->display->print(line1);
    this->display->setCursor(15, 90);
    this->display->print(line2);
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
    bheadershow = false; // header has been showed
  }
}

/***********************************************************************
 * Function: Heat67LCD_Header()
 * Description: Static header for the "heating amplifier block and sensor"
 *  screen (shown once per bheadershow). Delegates to drawHeat67Header().
 * pramameter: none
 *  return: none
 */
void displayCLD::Heat67LCD_Header()
{
  drawHeat67Header("Heating amplifier ", "block and sensor");
}

/***********************************************************************
 * Function: Heat67LCD()
 * Description: Dynamic refresh of the "heating to 67C" screen. On each new
 *  temperature sample it redraws the four-value readout (bottom[1], bottom[2],
 *  hotlid[0], hotlid[1]) in RED. Once all four reach their targets
 *  (amplifTemp-1.5 / HOTLID23_TEMP-2) it switches type_infor to epreheat67.
 * pramameter: none
 *  return: none
 */
void displayCLD::Heat67LCD()
{
  if (_bottomThermometer.getNewTemperatureScreenFlag()) // if there is temperature data to display
  {
    this->display->fillRect(90, 160, 180, 50, BLACK);
    this->display->setCursor(90, 190);
    double *temperature = _PIDControl.getBottomTemperature();
    double *temperatureHotlid = _PIDControl.getHotlidTemperature();
    this->display->setTextSize(2);
    this->display->setTextColor(RED);
    this->display->printf("%d:%d:%d:%d", int(temperature[1]), int(temperature[2]), int(temperatureHotlid[0]), int(temperatureHotlid[1]));
    _bottomThermometer.clearNewTemperatureScreenFlag();                   // clear the flag after display the temperature
    if ((temperature[1] >= (_ForteSetting.parameter.amplifTemp - 1.5))    //
        && (temperature[2] >= (_ForteSetting.parameter.amplifTemp - 1.5)) //
        && (temperatureHotlid[0] >= (HOTLID23_TEMP - 2))                  //
        && (temperatureHotlid[1] >= (HOTLID23_TEMP - 2)))                 //
    {
      _displayCLD.type_infor = epreheat67; // if the temperature of heater and hotlid is ready, then go to next screen
      _displayCLD.bheadershow = true;      // set the flag to show header
      _displayCLD.changeScreen = true;     // set the flag to change screen
    }
  }
}

/***********************************************************************
 * Function: Preheat67LCD_Header()
 * Description: Static header for the "waiting for sensor to warm up" screen
 *  (shown once per bheadershow). Delegates to drawHeat67Header().
 * pramameter: none
 *  return: none
 */
void displayCLD::Preheat67LCD_Header()
{
  drawHeat67Header("Waiting for sensor", "to warm up !!!");
  this->display->setTextSize(1);
  this->display->setTextColor(GREEN);
  this->display->setCursor(30, 235);
  this->display->printf("Press greenbutton to skip preheat");
}

/***********************************************************************
 * Function: Preheat67LCD()
 * Description: Dynamic refresh of the "waiting for sensor" screen. On each new
 *  temperature sample it redraws the four-value readout (bottom[1], bottom[2],
 *  hotlid[0], hotlid[1]) in Forte_Green, then clears the new-temperature flag.
 * pramameter: none
 *  return: none
 */
void displayCLD::Preheat67LCD()
{
  if (_bottomThermometer.getNewTemperatureScreenFlag()) // if there is temperature data to display
  {
    this->display->fillRect(90, 160, 180, 50, BLACK);
    this->display->setCursor(90, 190);
    double *temperature = _PIDControl.getBottomTemperature();
    double *temperatureHotlid = _PIDControl.getHotlidTemperature();
    this->display->setTextSize(2);
    this->display->setTextColor(Forte_Green);
    this->display->printf("%d:%d:%d:%d", int(temperature[1]), int(temperature[2]), int(temperatureHotlid[0]), int(temperatureHotlid[1]));
    _bottomThermometer.clearNewTemperatureScreenFlag(); // clear the flag after display the temperature
  }
}

/***********************************************************************
 * Function: calibPreheatStartLCD()
 * Description: Calib flow p0 prompt. Header "Calib preheat 55C / Press RED to
 *  start" plus a live heater2:heater3 temperature readout. Heaters are still
 *  off here; pressing RED starts the preheat (handled in button.cpp).
 * pramameter: none
 *  return: none
 */
void displayCLD::calibPreheatStartLCD()
{
  drawHeat67Header("RPL preheat 55*C", "before calibration");
  // if (_bottomThermometer.getNewTemperatureScreenFlag())
  // {
  this->display->fillRect(90, 160, 180, 50, BLACK);
  this->display->setCursor(90, 190);
  // double *temperature = _PIDControl.getBottomTemperature();
  this->display->setTextSize(2);
  this->display->setTextColor(RED);
  this->display->setCursor(90, 175);
  this->display->printf("Press RED to");
  this->display->setCursor(90, 205);
  this->display->printf("start preheat");
  this->changeScreen = false; // set the flag to change screen
  //   _bottomThermometer.clearNewTemperatureScreenFlag();
  // }
}

/***********************************************************************
 * Function: calibPreheatingLCD()
 * Description: Calib flow p0 heating screen. Header "Preheating 55C / please
 *  wait" plus the live heater2:heater3 readout (RED). Does not transition by
 *  itself; the PID (calibPreheat55) advances type_infor to ecalibSelect after
 *  reaching 55C and holding 5 minutes.
 * pramameter: none
 *  return: none
 */
void displayCLD::calibPreheatingLCD()
{
  drawHeat67Header("Preheating 55C", "please wait ...");
  if (_bottomThermometer.getNewTemperatureScreenFlag())
  {
    this->display->fillRect(90, 160, 180, 50, BLACK);
    this->display->setCursor(90, 190);
    double *temperature = _PIDControl.getBottomTemperature();
    this->display->setTextSize(2);
    this->display->setTextColor(RED);
    this->display->printf("%d : %d", int(temperature[1]), int(temperature[2]));
    _bottomThermometer.clearNewTemperatureScreenFlag();
  }
}

/***********************************************************************
 * Function: calibSelectLCD()
 * Description: Calib flow p1 choice menu (drawn once). BLUE = Calibration,
 *  RED = Amplification. Heaters keep maintaining 55C while this is shown.
 * pramameter: none
 *  return: none
 */
void displayCLD::calibSelectLCD()
{
  this->display->fillScreen(BLACK);
  this->display->drawRoundRect(8, 0, 305, 240, 10, Forte_Green);
  this->display->drawRoundRect(8, 40, 305, 170, 0, Forte_Green);

  this->display->setTextColor(WHITE);
  this->display->setTextSize(2);
  this->display->setCursor(60, 30);
  this->display->println("Preheated 55C");

  this->display->setTextSize(2);
  this->display->setTextColor(GREEN);
  this->display->setCursor(40, 100);
  this->display->print("Calibration");
  this->display->setTextColor(RED);
  this->display->setCursor(40, 140);
  this->display->print("Amplification");

  this->display->setTextSize(1);
  this->display->setTextColor(GREEN);
  this->display->setCursor(20, 230);
  this->display->print("Green: Calib");
  this->display->setTextColor(RED);
  this->display->setCursor(200, 230);
  this->display->print("Red: Amp");
}

/***********************************************************************
 * Function: preHeat80CLD_Header()
 * Description: Draws the static header for the 80C preheat screen once
 *  per show (guarded by bheadershow): "Heat up to 80 in about 10min"
 *  with hazard band, warning box and circle, then clears bheadershow.
 * pramameter: none
 *  return: none
 */
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

/***********************************************************************
 * Function: preHeat80CLD()
 * Description: Dynamic refresh part of the 80C preheat screen. When a new
 *  temperature sample is ready it redraws the single bottom heater
 *  temperature (bottomTemperature[0]) and clears the new-temperature flag.
 * pramameter: none
 *  return: none
 */
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

/***********************************************************************
 * Function: waitLysisTube()
 * Description: Rate-limited screen (refreshes every 10s via timeRefresh)
 *  prompting the user to put the lysis tube in and close the lid. In the
 *  English branch it draws the instructions, warning box/circle and the
 *  "Press Red to Start Lysis" prompt; the language==0 branch is empty.
 * pramameter: none
 *  return: none
 */
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

/***********************************************************************
 * Function: waitLysis10min()
 * Description: Manages the lysis heating countdown. When timer10minEnd is
 *  reached it advances type_infor to ewaitphase2, sets changeScreen and
 *  beeps. Otherwise, throttled to ~1s, it draws the "Heating..." header
 *  once (bheadershow) and continuously updates the remaining minutes
 *  ("Time left"). The language==0 branch is empty.
 * pramameter: none
 *  return: none
 */
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

/***********************************************************************
 * Function: startHeating10mins()
 * Description: Initializes the lysis heating countdown by setting
 *  timer10minEnd to now + LYSIS_DURATION seconds, re-arming bheadershow
 *  so the header redraws, and resetting timeRefresh to 0 to force an
 *  immediate refresh.
 * pramameter: none
 *  return: none
 */
void displayCLD::startHeating10mins()
{
  timer10minEnd = millis() + LYSIS_DURATION * 1000; // calculate the end time of the 10mins
  bheadershow = true;                               // use it again to show header of the display
  timeRefresh = 0;
}

/***********************************************************************
 * Function: waitBtnStartPhase2()
 * Description: Rate-limited screen (refreshes every 10s) prompting the
 *  user, after lysis, to take out the lysis tube, close the lid and
 *  "Press Green to preheat 67". Drawn with GREEN warning box/circle in
 *  the English branch; the language==0 branch is empty.
 * pramameter: none
 *  return: none
 */
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

/***********************************************************************
 * Function: waitAmpTube()
 * Description: Rate-limited screen (refreshes every 10s) prompting the
 *  user to put the amplification tube in and close the lid, with a RED
 *  warning box/circle and the "Press Red to Measure" prompt in the
 *  English branch; the language==0 branch is empty.
 * pramameter: none
 *  return: none
 */
void displayCLD::waitAmpTube()
{
  unsigned long now = millis();
  if (timeRefresh > now) // no refresh needed
  {
    return;
  }
  timeRefresh = now + 10 * 1000; // refresh every 10 seconds

  // _buzzer.BuzzerAlert();
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
  }
}

/***********************************************************************
 * Function: startAmplification()
 * Description: Initializes the amplification countdown by setting
 *  timer30minEnd to now + AMPLIFICATION_DURATION, re-arming bheadershow
 *  so the header redraws, and resetting timeRefresh to 0 to force an
 *  immediate refresh.
 * pramameter: none
 *  return: none
 */
void displayCLD::startAmplification()
{
  timer30minEnd = millis() + AMPLIFICATION_DURATION; // * 1000;//_sensor6035.getOPTO_DURATION();//AMPLIFICATION_DURATION*60*1000;   //calculate the end time of the 10mins
  bheadershow = true;                                // use it again to show header of the display
  timeRefresh = 0;
}

/***********************************************************************
 * Function: waitAmplification30min()
 * Description: Runs the amplification countdown screen. Returns once
 *  timer30minEnd is reached. Otherwise, throttled to ~1s, on the first
 *  pass (bheadershow) it calls _sensor6035.setStepeSensorstart() and
 *  draws the "Amplification.." header, then continuously updates the
 *  remaining minutes ("Time left"). The language==0 branch is empty.
 * pramameter: none
 *  return: none
 */
void displayCLD::waitAmplification30min()
{
  unsigned long now = millis();
  if (timer30minEnd < now) // if reached 30mins
  {
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
  this->display->fillRect(18, 150, 320, 100, BLACK);
  this->display->setCursor(90, 190);
  unsigned long timeleft = (timer30minEnd - now) / 1000;     // seconds left
  this->display->printf("%d Minute", (timeleft / (60) + 1)); // show the time left
}

/***********************************************************************
 * Function: prepare()
 * Description: Rate-limited (every 10s) "put the tube and close the lid"
 *  prompt before measuring. The language==0 branch draws the Vietnamese
 *  version including "để đo lần <couter>" (measurement count); the else
 *  branch draws the English "Press Red to measure" version. Both use the
 *  RED warning box/circle layout.
 * pramameter: none
 *  return: none
 */
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

/***********************************************************************
 * Function: screen_Result()
 * Description: Computes and renders the amplification results screen. Ends
 *  Bluetooth, reads amplification data from EEPROM, streams the per-cycle
 *  calibrated sensor output over the info channel, and retries WiFi up to
 *  50 times. When WiFi is connected and key=='f' it posts results to the
 *  Google Sheet; otherwise it computes results locally via bResultGet.
 *  Then it draws the L/R 10-channel grid, color-coding each channel by its
 *  result/error code (P=RED, S=YELLOW, N=Forte_Green dashes, errors in
 *  ORANGE, B=CYAN), posts errors if any when key=='f', shows "Press white
 *  key to test next" and clears changeScreen.
 * pramameter: key - mode selector: 'f' (finished: post data and errors to
 *  the sheet), other values compute results locally without posting
 *  return: none
 */
void displayCLD::screen_Result(char key)
{
  {
    float CT_value[10] = {0};
    char result[10] = {0};
    uint8_t loops = _ForteSetting.parameter.amplification_time;
    // Fully release the Bluetooth Classic stack (not just SerialBT.end()) here.
    // SerialBT.end() alone leaves the controller + bluedroid (~60KB) resident and
    // FRAGMENTING the heap, so the later HTTPS upload can't get a big enough
    // contiguous block for the mbedTLS handshake (-32512 / SSL alloc failed).
    // releaseBluetoothStack() hands that ~60KB back and is idempotent.
    releaseBluetoothStack();

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
    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 50)
    {
      delay(100);
      retries++;
      Serial.print(".");
      WiFi.begin(ssid.c_str(), password.c_str());
    }

    /* Post data and errors to Google Sheet */
    if ((WiFi.status() == WL_CONNECTED) && (key == 'f'))
    {
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

      /* Check Sensor Errors */
      if (error.searchError(errorLightSensor, errorNoData, eSensor1stReading, i) != 255 ||
          error.searchError(errorLightSensor, errorWrongData, eSensor1stReading, i) != 255 ||
          error.searchError(errorLightSensor, errorTooDark, eSensor1stReading, i) != 255 ||
          error.searchError(errorLightSensor, errorTooBright, eSensor1stReading, i) != 255)
      {
        if (result[i] == 'P' || result[i] == 'S')
        {
          this->display->setTextColor(ORANGE);
          this->display->printf("|%2.0f/E|", CT_value[i]);
        }
        else
        {
          this->display->setTextColor(ORANGE);
          this->display->printf("|  /E|", CT_value[i]);
        }
      }
      else
      {
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
    }

    if ((error.numUnit != 0) && (key == 'f'))
    {
      postError_fullGoogleSheet();
    }

    this->display->setTextColor(WHITE);
    this->display->setTextSize(1);
    this->display->setCursor(15, 230);
    this->display->printf("Press white key to test next");

    // this->display->drawBitmap(280, 210, play_hover, 19, 20, RED);
    this->display->fillTriangle(305, 230, 305, 220, 320, 225, RED);

    changeScreen = false;
  }
}

/***********************************************************************
 * Function: screen_errorResult()
 * Description: Draws the sensor-error results grid (L/R, 10 channels).
 *  For each channel it searches for an errorLightSensor/errorNoData on the
 *  first reading: if found it prints the encoded error code in ORANGE,
 *  otherwise it shows CYAN dashes. Ends with "Press white key to test
 *  next" and clears changeScreen.
 * pramameter: none
 *  return: none
 */
void displayCLD::screen_errorResult(void)
{
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

    uint8_t tmp = error.searchError(errorLightSensor, errorNoData, eSensor1stReading, i);
    /* Check Sensor Errors */
    if (tmp != 255)
    {
      this->display->setTextColor(ORANGE);
      this->display->printf("|%d|", error.EncodeError(error.error[tmp]));
    }
    else
    {
      this->display->setTextColor(CYAN);
      this->display->printf("|----|");
    }
  }

  this->display->setTextColor(WHITE);
  this->display->setTextSize(1);
  this->display->setCursor(15, 230);
  this->display->printf("Press white key to test next");

  changeScreen = false;
}
// }

/***********************************************************************
 * Function: set_connect_bluetooth()
 * Description: WiFi setup over Bluetooth flow. Shows the current WiFi ID,
 *  password and ID (Vietnamese "Cài đặt WIFI" when language==0, else
 *  English "WIFI Set up"), then calls connectWIFI() to receive new
 *  credentials over Bluetooth, saves them to EEPROM via saveSettingDevice()
 *  and reloads them, displays a success message and the updated values,
 *  then restarts the ESP.
 * pramameter: none
 *  return: none
 */
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

/***********************************************************************
 * Function: settingSucces()
 * Description: Free helper that clears the screen and prints the given
 *  title in GREEN size-2 text (used as a transient "success"/status
 *  banner), then waits 500ms.
 * pramameter: title - the message string to display
 *  return: none
 */
void settingSucces(String title)
{
  _displayCLD.display->fillScreen(BLACK);
  _displayCLD.display->setTextSize(2);
  _displayCLD.display->setCursor(25, 120);
  _displayCLD.display->setTextColor(GREEN);
  _displayCLD.display->print(title);

  delay(500);
}

/***********************************************************************
 * Function: loop()
 * Description: Main display state machine. When changeScreen is set it
 *  switches on type_infor and dispatches to the matching screen handler
 *  (start, preheat 67/80, wait lysis/amp tube, heating/amplification
 *  countdowns, prepare, result/error/finished/review, restart/reboot
 *  flows, timeout-to-start, settings menus, WiFi/Bluetooth setup, data
 *  upload, and the calibration / LED-power / OTA screens). It clears
 *  changeScreen for screens that should draw once, and at the end always
 *  refreshes the WiFi icon.
 * pramameter: none
 *  return: none
 */
void displayCLD::loop()
{
  if (this->changeScreen)
  {
    // this->display->begin();
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

    case eheating67:
    {
      dbg_display("eheating67");
      this->Heat67LCD_Header();
      this->Heat67LCD();
      break;
    }
    case epreheat67:
    {
      dbg_display("waitingpreheat67");
      this->Preheat67LCD_Header();
      this->Preheat67LCD();
      break;
    }
    case ecalibPreheatStart:
    {
      dbg_display("ecalibPreheatStart");
      this->calibPreheatStartLCD();
      break; // keep refreshing the temperature readout
    }
    case ecalibPreheating:
    {
      dbg_display("ecalibPreheating");
      this->calibPreheatingLCD();
      break; // keep refreshing the temperature readout
    }
    case ecalibSelect:
    {
      dbg_display("ecalibSelect");
      this->calibSelectLCD();
      this->changeScreen = false; // static menu, draw once
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
    case escreenErrorResult:
    {
      dbg_display("escreenErrorResult");
      this->screen_errorResult();
      break;
    }
    case escreenFinished:
    {
      this->screen_Result('f');
      _PIDControl.rerun();
      _sensor6035.rerun();
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
      _PIDControl.rerun(); // Reset PID state on restart
      _sensor6035.rerun();
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
  }
}

/***********************************************************************
 * Function: rerun()
 * Description: Requests a screen redraw on the next loop() by setting the
 *  changeScreen flag to true (without changing type_infor).
 * pramameter: none
 *  return: none
 */
void displayCLD::rerun()
{
  // type_infor = escreenStart;
  changeScreen = true;
  // info_displayf("status of LCD is %d\n", type_infor);
}

/***********************************************************************
 * Function: setting_Menu()
 * Description: Draws the "RAPID Settings" menu screen with the FORTE
 *  BIOTECH header and three option boxes: GREEN "Wifi/Update", RED
 *  "Up Data" and WHITE "Bluetooth".
 * pramameter: none
 *  return: none
 */
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

/***********************************************************************
 * Function: setting_Wifi()
 * Description: WiFi/device settings screen. Normalizes empty ssid,
 *  password and id_device to a space, draws the "Settings Device/Update"
 *  screen showing the current wifi name, password and id device, then
 *  calls Wifi_Connect() to obtain new values, redraws the updated values,
 *  waits 1s and performs esp_restart().
 * pramameter: none
 *  return: none
 */
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

/***********************************************************************
 * Function: setting_Language()
 * Description: Placeholder for a language settings screen. Currently
 *  empty; performs no action.
 * pramameter: none
 *  return: none
 */
void displayCLD::setting_Language(void)
{
}

/* Function Calib */
/***********************************************************************
 * Function: display_Select_menu_calib()
 * Description: Draws the "Select Mode" calibration entry menu with a
 *  GREEN "Calibration", RED "Amplification" and "Tube 0" options, plus a
 *  "Back" hint in the corner.
 * pramameter: none
 *  return: none
 */
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
/***********************************************************************
 * Function: display_Select_mode()
 * Description: Draws the "Select Mode" screen offering a GREEN
 *  "Calibration" and a RED "Setting LED power" option, plus a "Back"
 *  hint in the corner.
 * pramameter: none
 *  return: none
 */
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

/***********************************************************************
 * Function: display_Select_slot()
 * Description: Calibration slot selection screen. Shows the currently
 *  selected slot number (slot + 1) and its stored parameters (slope,
 *  origin, LED power) for that slot, with "Select", "Next" and "Exit"
 *  button hints at the bottom.
 * pramameter: none
 *  return: none
 */
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

/***********************************************************************
 * Function: display_Calib()
 * Description: Prompts the user to insert the calibration tube. The
 *  concentration label printed depends on _sensor6035.type_calib (0=300,
 *  1=200, 2=100, 3=0) and the target slot is shown as slot + 1, with a
 *  GREEN "Calib" button hint.
 * pramameter: none
 *  return: none
 */
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

/***********************************************************************
 * Function: display_Waiting_Calib()
 * Description: Shows the "Calibrating" / "Waitting..." progress screen
 *  while a calibration measurement is in progress.
 * pramameter: none
 *  return: none
 */
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

/***********************************************************************
 * Function: display_Calib_Complete()
 * Description: Shows the calibration result: Slope, RSQ and Origin from
 *  _sensor6035.cal_calib, coloring out-of-range slope (<0.5 or >3.5) and
 *  low RSQ (<0.95) in RED. If any of those are out of range it shows
 *  "Failed Calib!" with "Calib again"/"Setting LED" hints; otherwise it
 *  sets flag_calib_done, shows "Done Calib!" and calls set_flag_calib().
 * pramameter: none
 *  return: none
 */
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

/***********************************************************************
 * Function: display_Set_powerled()
 * Description: "Setting LED" screen for editing the 3-digit LED power
 *  value. Displays the three digits (led_power[0..2]) and draws a WHITE
 *  selection arrow under the digit currently pointed at by this->index,
 *  with "Next", "Up" and "Save" button hints.
 * pramameter: none
 *  return: none
 */
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
/***********************************************************************
 * Function: calculate()
 * Description: Combines the three LED-power digits into a single integer,
 *  stores it into _ForteSetting.parameter.led_power[slot], persists the
 *  parameter block to EEPROM, and shows the "Saved LED power!" screen
 *  with a "Next" hint.
 * pramameter: none
 *  return: none
 */
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
  // Mark the block valid (length == sizeof) so begin() loads it on the next boot.
  _ForteSetting.parameter.length = sizeof(_ForteSetting.parameter);
  EEPROM.begin(_EEPROM_SIZE);
  EEPROM.put(PARAMETERPOS, _ForteSetting.parameter);
  EEPROM.commit();
  EEPROM.end();

  this->display->setTextSize(1);
  this->display->setTextColor(GREEN);
  this->display->setCursor(20, 230);
  this->display->print("Next");
}

/***********************************************************************
 * Function: saving_calib()
 * Description: Saves the computed calibration slope (_sensor6035.cal_calib[0])
 *  into _ForteSetting.parameter.slopes[slot], persists the parameter block
 *  to EEPROM, and shows the "Saved calibration!" screen with a "Next" hint.
 * pramameter: none
 *  return: none
 */
void displayCLD::saving_calib(void)
{
  this->display->fillScreen(BLACK);
  this->display->drawRoundRect(8, 0, 305, 240, 10, Forte_Green);

  this->display->setTextSize(2);
  this->display->setTextColor(WHITE);
  this->display->setCursor(40, 90);
  this->display->print("Saved calibration!");

  _ForteSetting.parameter.slopes[this->slot] = _sensor6035.cal_calib[0];
  // Mark the block valid (length == sizeof) so begin() loads it on the next boot.
  _ForteSetting.parameter.length = sizeof(_ForteSetting.parameter);
  EEPROM.begin(_EEPROM_SIZE);
  EEPROM.put(PARAMETERPOS, _ForteSetting.parameter);
  EEPROM.commit();
  EEPROM.end();

  this->display->setTextSize(1);
  this->display->setTextColor(GREEN);
  this->display->setCursor(20, 230);
  this->display->print("Next");
}

/***********************************************************************
 * Function: set_flag_calib()
 * Description: After a successful calibration, waits 3 seconds then
 *  transitions the global _displayCLD state to eSaveCalib and requests a
 *  screen change (changeScreen = true) so the save step runs next.
 * pramameter: none
 *  return: none
 */
void displayCLD::set_flag_calib(void)
{
  delay(3000);
  _displayCLD.type_infor = eSaveCalib;
  _displayCLD.changeScreen = true;
}

/***********************************************************************
 * Function: display_UpdateOTA()
 * Description: Shows the OTA update prompt: "You have a new update!" with
 *  the new firmware version (fwVer) and detail (fwCont), and instructions
 *  to press red to Update or green to Skip.
 * pramameter: none
 *  return: none
 */
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

/***********************************************************************
 * Function: waittingUpdate()
 * Description: Shows the "Waiting..." screen displayed while an OTA
 *  firmware update is being downloaded/applied.
 * pramameter: none
 *  return: none
 */
void displayCLD::waittingUpdate(void)
{
  this->display->fillScreen(BLACK);
  this->display->drawRoundRect(8, 0, 305, 240, 10, Forte_Green);

  this->display->setTextSize(2);
  this->display->setTextColor(WHITE);
  this->display->setCursor(40, 100);
  this->display->print("Waiting...");
}

displayCLD _displayCLD;
