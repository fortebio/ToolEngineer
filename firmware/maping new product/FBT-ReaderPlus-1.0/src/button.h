#ifndef _BUTTON_H_
#define _BUTTON_H_

#include "define.h"
#include "bluetooth.h"
#include "displayLCD.h"
#include "Ticker.h"
#include "sensor.h"
#include "displayresources.h"
#include <EEPROM.h>

typedef enum {
  B_RED,
  B_GREEN,
  B_WHITE
} e_statusbutton;

class buttonManager {
private:
  /* data */
public:
  buttonManager(/* args */);
  ~buttonManager();

  bool buttonRed;
  bool buttonGreen;
  bool buttonWhite;
};

extern String measure_value;
extern String id_BLE;
extern String Sample_measure;

#endif
