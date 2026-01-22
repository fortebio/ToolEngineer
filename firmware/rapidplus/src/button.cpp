#include "button.h"
#include "Ticker.h"
#include "displayCLD.h"
#include "PIDControl.h"
// #include "Bluetooth.h"
typedef void (*hanler)();
void buttonRedHandler();
void buttonBlueHandler();
void buttonWhiteHandler();

static void tickerHandler(uint8_t index);
static void tickerHandler1(uint8_t index);

/* Function calib */
static void tickerHandler2(uint8_t index);

static uint8_t buttons[NumberButton];
static hanler Hanler[NumberButton];
static bool buttonPressed[NumberButton];
static Ticker buttonTicker[NumberButton];
static unsigned long timeAtPress[NumberButton];

buttonManager::buttonManager(/* args */)
{
  buttons[0] = BUTTON_RED;
  Hanler[0] = &buttonRedHandler;
  buttons[1] = BUTTON_BLUE;
  Hanler[1] = &buttonBlueHandler;
  buttons[2] = BUTTON_WHITE;
  Hanler[2] = &buttonWhiteHandler;
}

void buttonManager::buttonStart()
{
  for (int i = 0; i < NumberButton; i++)
  {
    pinMode(buttons[i], INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(buttons[i]), Hanler[i], CHANGE);
  }
}

buttonManager::~buttonManager()
{
}

void buttonProcess(e_statusbutton index)
{

  if (!digitalRead(buttons[index]) && (buttonPressed[index] == false))
  {
    timeAtPress[index] = millis();
    if (index == B_WHITE)
    {
      buttonTicker[index].attach_ms(calibTime, &tickerHandler, (uint8_t)index);
    }
    if (index == B_RED)
    {
      buttonTicker[index].attach_ms(3000, &tickerHandler1, (uint8_t)index);
    }
    if (index == B_BLUE)
    {
      buttonTicker[index].attach_ms(3000, &tickerHandler2, (uint8_t)index);
    }
    buttonPressed[index] = true;
  }

  else if ((buttonPressed[index] == true) && ((unsigned long)(millis() - timeAtPress[index]) > TimePressAnti) && ((unsigned long)(millis() - timeAtPress[index]) < calibTime))
  {

    _buzzer.BuzzerStop(); // stop the buzzer if any button is pressed

    buttonPressed[index] = false;
    switch (index)
    {
    case B_RED:
    {
      if (_displayCLD.ErrorStatus())
      {
        return;
      }
      if (_displayCLD.type_infor == ewaitLysisTube)
      {
        _displayCLD.type_infor = eheatLysis;
        _displayCLD.changeScreen = true;
        // _displayCLD.timeRefresh = 0;
        _displayCLD.startHeating10mins();
        dbg_button("Red Btn - start heating lysis");
      }

      else if (_displayCLD.type_infor == escreenStart)
      { // skip to amplification stage directly if pressing red at the beginning
        _displayCLD.type_infor = epreheating67;
        _displayCLD.bheadershow = true;
        _displayCLD.changeScreen = true;
        _PIDControl.setPreheat67(); // check the current temperature is not over heat
        _sensor6035.setStepeSensorpreheat();
        dbg_button("red button - start amplification");
      }
      
      else if (_displayCLD.type_infor == eSelectAmpli)
      {
        _PIDControl.heatSimulation(0xFF);
        _PIDControl.setPID23Ready();
        _displayCLD.type_infor = ewaitampTube;
        _displayCLD.bheadershow = true;
        _displayCLD.changeScreen = true;
      }
      else if (_displayCLD.type_infor == ewaitampTube)
      {
        _displayCLD.type_infor = eoptoreading;
        _displayCLD.changeScreen = true;
        // _displayCLD.timeRefresh = 0;
        _displayCLD.startAmplification();
        dbg_button("Red Btn - start amplification");
      }
      else if (_displayCLD.type_infor == eSettingMenu)
      {
        _displayCLD.type_infor = eUpLoadData;
        _displayCLD.changeScreen = true;
      }

      else if (_displayCLD.type_infor == eSelectSlot)
      {
        _displayCLD.slot++;
        if (_displayCLD.slot == 10)
        {
          _displayCLD.slot = 0;
        }
        _displayCLD.type_infor = eSelectSlot;
        _displayCLD.changeScreen = true;
      }
      else if (_displayCLD.type_infor == eSelectMode)
      {
        _displayCLD.type_infor = eSetPowerLed;
        _displayCLD.changeScreen = true;
      }
      else if (_displayCLD.type_infor == eCalibComplete && !(_displayCLD.flag_calib_done))
      {
        _displayCLD.type_infor = eSetPowerLed;
        _displayCLD.changeScreen = true;
      }
      
      else if (_displayCLD.type_infor == eSetPowerLed)
      {
        switch (_displayCLD.index)
        {
          case 0:
          {
            _displayCLD.led_power[0]++;
            if (_displayCLD.led_power[0] > 9)
            {
              _displayCLD.led_power[0] = 0;
            }
            break;
          }
          case 1:
          {
            _displayCLD.led_power[1]++;
            if (_displayCLD.led_power[1] > 9)
            {
              _displayCLD.led_power[1] = 0;
            }
            break;
          }
          case 2:
          {
            _displayCLD.led_power[2]++;
            if (_displayCLD.led_power[2] > 9)
            {
              _displayCLD.led_power[2] = 0;
            }
            break;
          }
          
          default:
            break;
        }
        _displayCLD.type_infor = eSetPowerLed;
        _displayCLD.changeScreen = true;
      }
      else if (_displayCLD.type_infor == eUpdateOTA)
      {
        flag_check_Update = false;
        flagUpdate = true;
      }
      break;
    }

    case B_BLUE:
    {
      if (_displayCLD.ErrorStatus())
      {
        return;
      }
      if (_displayCLD.type_infor == escreenStart)
      {
        _displayCLD.type_infor = epreheating80; // actually should start from 80 degree
        _displayCLD.changeScreen = true;
        _displayCLD.bheadershow = true;
        _PIDControl.setpid1startpreHeat80(); // check the current temperature is not over heat
        dbg_button("green button - start heating to 80");
      }
      else if (_displayCLD.type_infor == ewaitphase2)
      {
        _displayCLD.type_infor = epreheating67;
        _displayCLD.bheadershow = true;
        _displayCLD.changeScreen = true;
        _PIDControl.setPreheat67(); // check the current temperature is not over heat
        _sensor6035.setStepeSensorpreheat();
        dbg_button("green button - start heating to 67");
      }

      else if (_displayCLD.type_infor == epreheating67)
      {
        _sensor6035.skip2Maintain();
        dbg_button("green button - skip opto preheat");
      }
      /** test postData to GoogleSheets */
      else if (_displayCLD.type_infor == eSettingMenu)
      {
        _displayCLD.type_infor = eSettingWifi;
        _displayCLD.changeScreen = true;
      }

      else if (_displayCLD.type_infor == eSelectAmpli)
      {
        _displayCLD.type_infor = eSelectSlot;
        _displayCLD.changeScreen = true;
      }
      else if (_displayCLD.type_infor == eSelectSlot)
      {
        _displayCLD.type_infor = eSelectMode;
        _displayCLD.changeScreen = true;
      }
      else if (_displayCLD.type_infor == eSelectMode)
      {
        _displayCLD.type_infor = eCalibrating;
        _displayCLD.changeScreen = true;
      }
      else if (_displayCLD.type_infor == eCalibrating)
      {
        _sensor6035.setStepeSensorcalib();
      }
      else if (_displayCLD.type_infor == eSetPowerLed)
      {
        _displayCLD.index++;
        if (_displayCLD.index > 2)
        {
          _displayCLD.index = 0;
        }
        _displayCLD.type_infor = eSetPowerLed;
        _displayCLD.changeScreen = true;
      }
      else if (_displayCLD.type_infor == eCalibComplete && !(_displayCLD.flag_calib_done))
      { 
          _displayCLD.type_infor = eCalibrating;
          _displayCLD.changeScreen = true;
      }
      else if (_displayCLD.type_infor == eSavePowerLed)
      {
        _displayCLD.type_infor = eSelectSlot;
        _displayCLD.changeScreen= true;
      }
      else if (_displayCLD.type_infor == eSaveCalib)
      {
        _displayCLD.type_infor = eSelectSlot;
        _displayCLD.changeScreen= true;
        _displayCLD.flag_calib_done = false;
      }
      else if (_displayCLD.type_infor == eUpdateOTA)
      {
        flag_check_Update = false;
        flagUpdate = false;
        _displayCLD.type_infor = escreenStart;
        _displayCLD.changeScreen = true;
      }
      break;
    }

    case B_WHITE:
    {
      if (_displayCLD.FinishStatus()) // button pressed when display the result or error status, then return to start
      {
        _displayCLD.type_infor = escreenRestart;
        _displayCLD.changeScreen = true;
        return;
      }
      if (_displayCLD.ErrorStatus())
      {
        return;
      }
      if (_displayCLD.type_infor == eSettingMenu)
      {
        _displayCLD.type_infor = eSettingBluetooth;
        _displayCLD.changeScreen= true;
        return;
      }
      if (_displayCLD.type_infor == eSetPowerLed)
      {
        _displayCLD.type_infor = eSavePowerLed;
        _displayCLD.changeScreen= true;
        return;
      }
      if (_displayCLD.type_infor == eSelectMode)
      {
        _displayCLD.type_infor = eSelectSlot;
        _displayCLD.changeScreen= true;
        return;
      }
      if (_displayCLD.type_infor == eSelectSlot)
      {
        ESP.restart();
      }
      if (_displayCLD.type_infor == escreenStart) // no need to restart as at the start screen already
      {
        return;
      }
      _displayCLD.type_infor = ebuttonrestart;
      _displayCLD.changeScreen = true;
      return;

      break;
    }

    default:
      break;
    }
  }
  else
  {
    buttonPressed[index] = false;
  }
}

static void tickerHandler(uint8_t index)
{
  buttonTicker[index].detach();

  if (!digitalRead(buttons[index]))
  {
    buttonPressed[index] = false;
    _displayCLD.changeScreen = true;
    _displayCLD.type_infor = escreenReview;
    dbg_button("nut WHITE huhu");
  }
}
static void tickerHandler1(uint8_t index)
{
  buttonTicker[index].detach();
  if (!digitalRead(buttons[index]))
  {
    buttonPressed[index] = false;
    _displayCLD.type_infor = eSettingMenu;
    _displayCLD.changeScreen = true;
    dbg_button("nut Red - setting");
  }
}

/* Function Calib */
static void tickerHandler2(uint8_t index)
{
  buttonTicker[index].detach();
  if (!digitalRead(buttons[index]))
  {
    buttonPressed[index] = false;
    _sensor6035.setStepeSensorwait();
    _displayCLD.type_infor = eSelectAmpli;
    _displayCLD.changeScreen = true;
    info_display("nut Green - calibrating");
  }
}


void IRAM_ATTR buttonRedHandler()
{
  buttonProcess(B_RED);
}

void IRAM_ATTR buttonBlueHandler()
{
  buttonProcess(B_BLUE);
}

void IRAM_ATTR buttonWhiteHandler()
{
  buttonProcess(B_WHITE);
}

// buttonManager _buttonManager;
