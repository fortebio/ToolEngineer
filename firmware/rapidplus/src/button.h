#ifndef _BUTTON_H
#define _BUTTON_H

#include "define.h"
#include "ForteSetting.h"
#include "sensor6035.h"
#include "updateOTA.h"

// typedef void (*hanler)();
typedef void (*buttonCallback)();
typedef enum {
    B_RED,
  B_BLUE,
  B_WHITE
} e_statusbutton;

class buttonManager
{
private:
    /* data */
public:
    buttonManager(/* args */);
    ~buttonManager();
    void buttonStart();
    // void handleButton();

    bool buttonRed;
    bool buttonGreen;
    bool buttonWhite;
};


#endif
