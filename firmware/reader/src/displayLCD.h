#ifndef _DISPLAY_LCD_H
#define _DISPLAY_LCD_H

#include "Arduino.h"
#include "define.h"
#include "displayresources.h"
#include "U8g2lib.h"
#include "Arduino_GFX_Library.h"
#include "sensor.h"
#include "Bluetooth.h"
#include "define.h"
#include "Bluetooth.h"
#include <SPI.h>
#include "menu.h"
#include "Sample.h"
// #include <Adafruit_GFX.h>
// #include <Adafruit_ILI9341.h>
// #include <U8g2_for_Adafruit_GFX.h>

#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 320

typedef enum
{
  escreenStart,
  ewaitingReadsensor,
  echoosetube,
  echooseSample,
  echooseAnimals,
  eprepare,
  escreenResult,
  escreenAverageResult,
  ecalibSensor,
  e_setting,
  e_settingWifi,
  e_settingUpdate,
  e_language,
  e_setThreshold,
  e_loginCalib,
  e_loginKeyUpdateCalib,
  e_loginUpdateNotification,
  THRESHOLD_SETTING_MANUAL,
  THRESHOLD_SETTING_BLE,
  logdata,
  e_groupData,
  e_updateThreshold,
  e_errorProcessing
} e_statuslcd;

class SetManual
{

private:
public:
  uint8_t index = 105;
  uint8_t value[4];
  uint16_t address = ADDR_THRESHOLD_BASE;
  uint8_t isick = 0;

  SetManual();
  SetManual(uint16_t addr)
  {
    uint16_t address = addr;
  };
  ~SetManual();
  void saveThresholdtoEEPROM(uint16_t address);
  uint16_t getThresholdformEEPROM(uint16_t address);
  void savevalueThreshold();
  void clearThreshold();
  void incrementValue(void);
  void decrementValue(void);
  void changeIndex(void);
};
class displayLCD
{
protected:
  /**
   * ILI9341 240x320
   * Frame Display use:
   *    cursorX: 20 pixel to 302 pixel
   *    cursorY: 0 pixel to 240 pixel
   */
  /* Align Text */
  int16_t getCursorX_textRight(const char *text, language_pointer language, uint8_t fontSize);  /* Right: return CursorX = 20 */
  int16_t getCursorX_textLeft(const char *text, language_pointer language, uint8_t fontSize);   /* Left: return CursorX = (320 - width) - 10 */
  int16_t getCursorX_textCenter(const char *text, language_pointer language, uint8_t fontSize); /* Center: return CursorX = (320 -widthText)/2 - 10 */

private:
public:
  displayLCD(/* args */);
  ~displayLCD();
  void begin();
  void logoFortebiotech();
  void screen_Start();
  void Choose_tube();
  /** */
  void Choose_tubePrawn();
  void Choose_tubeFish();
  void Choose_tubePig();
  void Choose_tubeChicken();

  void Choose_sample();
  /** */
  void Choose_samplePrawn();
  void Choose_sampleFish();
  void Choose_samplePig();
  void Choose_sampleChicken();
  void Choose_Animals();
  void screen_Complete();
  // void screen_Result();
  void screen_Average_Result();
  void waiting_Readsensor();
  char compare_result(uint32_t result);
  void prepare();
  void screen_Calib();
  void waiting_Calib();
  void waiting_SettingThreshold();
  void screen_Calib_Complete();
  void log_data();
  void set_language();
  void setting();
  void setting_threshold();
  void displaySettingWifi();
  void displaySettingUpdate();
  void updateThreshold();
  void updateKeyCalib();
  void setupThreshold();
  void displayErrorProcesssing();
  void displayGroupData();

  /* draw display*/
  void drawFrameDisplay(const char *title);
  void configFont(void);

  void loop();

  /* data */
  Arduino_ESP32SPI *bus;
  Arduino_GFX *display; //
  Menu *menu;

  e_statuslcd type_infor = escreenStart;
  int couter = 0;
  int instantStatus[2];
  bool changeScreen = true;
  language_pointer language_state;     // 0: Vietnamese 1: English
  language_pointer language = English; // 0:VietNamese 1: English
  // uint8_t step = PC;
  uint8_t step = 0;
  bool flag_postData = false;
};

extern displayLCD _displayLCD;
extern SetManual _Threshold;
extern SetManual _keyCalib;

#endif