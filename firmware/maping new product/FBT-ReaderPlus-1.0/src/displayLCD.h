#ifndef _DISPLAYLCD_H_
#define _DISPLAYLCD_H_

#include "Arduino.h"
#include "U8g2lib.h"
#include "Arduino_GFX_Library.h"
#include "sensor.h"
#include "bluetooth.h"
#include "displayresources.h"
#include "define.h"
#include "menu.h"
#include <SPI.h>

#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 320

typedef enum
{
  escreenStart,
  ewaitingReadsensor,
  echooseTube,
  echooseSample,
  eprepare,
  escreenResult,
  escreenAverageResult,
  ecalibSensor,
  e_setting,
  e_settingWifi,
  e_settingUpdate,
  e_language,
  e_setThreshold,
  THRESHOLD_SETTING_MANUAL,
  THRESHOLD_SETTING_BLE,
  e_updateThreshold,
  logdata
  //escreenchooseSlot
} e_statuslcd;

class Threshold {
private:
public:
  Threshold();
  ~Threshold();
  void saveThresholdtoEEPROM(uint8_t address);
  void clearThreshold();
  uint8_t index = 105;
  uint8_t index1 = 60;
  uint8_t valueThreshold[4];
  uint8_t address = ADDR_THRESHOLD_BASE;
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
  void choose_tube();
  void choose_sample();
  //void screen_Complete();
  //void choose_Sensor();
  void screen_Average_Result();
  void waiting_Readsensor();
  void compare_result(uint32_t result);
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
  void setupThreshold();

  /* draw display*/
  void drawFrameDisplay(const char *title);
  void configFont(void);

  void loop();

  /* data */
  Arduino_ESP32SPI *bus;
  Arduino_GFX *display;
  Menu *menu; 

  //  e_statuslcd type_infor = escreenStart;
  e_statuslcd type_infor = escreenStart;
  int counter = 0;
  int instantStatus[2];
  bool changeScreen = true;
  language_pointer language_state; // 0: Vietnamese 1: English
  language_pointer language = English;   // 0:VietNamese 1: English
  uint8_t step = PC;
  uint8_t step_slot = 0;
  bool flag_postData = false;
};
extern displayLCD _displayLCD;
extern Threshold _Threshold;

#endif