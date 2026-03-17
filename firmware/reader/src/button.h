#ifndef _BUTTON_H
#define _BUTTON_H

#include "define.h"
#include "Bluetooth.h"
#include "displayLCD.h"

// typedef void (*hanler)();
typedef void (*buttonCallback)();
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
  void handleButton();

  bool buttonRed;
  bool buttonGreen;
  bool buttonWhite;
};

extern String measure_value;
extern String Sample_measure;
extern String id_BLE;


#endif
