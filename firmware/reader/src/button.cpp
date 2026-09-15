#include "button.h"
#include "Ticker.h"
#include "displayLCD.h"
#include "sensor.h"
#include "Bluetooth.h"
#include <EEPROM.h>

typedef void (*hanler)();

void buttonRedHandler();
void buttonBlueHandler();
void buttonWhiteHandler();

static void tickerHandler(uint8_t index);
static void tickerHandler1(uint8_t index);
// static void tickerHandler2(uint8_t index);
// static void tickerHandlerGreen(uint8_t index);
static void tickerHandlerSaveEeprom(uint8_t index);
static void tickerHandlerUpdateEeprom(uint8_t index);
static void tickerHandlerEnterKeyCalib(uint8_t index);
static void tickerHandlerGroupData(uint8_t index);

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

  for (int i = 0; i < NumberButton; i++)
  {
    pinMode(buttons[i], INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(buttons[i]), Hanler[i], CHANGE);
  }
}

buttonManager::~buttonManager()
{
}

/**Tôi là người  việt Nam
 *
 */

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
      buttonTicker[index].attach_ms(calibTime, &tickerHandler1, (uint8_t)index);
    }
    if (index == B_GREEN)
    {
      if (_displayLCD.type_infor == e_updateThreshold)
      {
        buttonTicker[index].attach_ms(2000, &tickerHandlerSaveEeprom, (uint8_t)index);
      }
      else if (_displayLCD.type_infor == THRESHOLD_SETTING_MANUAL)
      {
        buttonTicker[index].attach_ms(2000, &tickerHandlerUpdateEeprom, (uint8_t)index);
      }
      else if (_displayLCD.type_infor == e_loginKeyUpdateCalib)
      {
        buttonTicker[index].attach_ms(2000, &tickerHandlerEnterKeyCalib, (uint8_t)index);
      }
      else if (_displayLCD.type_infor == echooseSample)
      {
        buttonTicker[index].attach_ms(2000, &tickerHandlerGroupData, (uint8_t)index);
      }
    }
    buttonPressed[index] = true;
  }

  else if ((buttonPressed[index] == true) && ((unsigned long)(millis() - timeAtPress[index]) > TimePressAnti) && ((unsigned long)(millis() - timeAtPress[index]) < calibTime) && blockSend == true)
  {
    buttonPressed[index] = false;
    switch (index)
    {
    case B_RED:
    {
      if (_sensor.flagback == true)
      {
        _displayLCD.couter++;
        if (_displayLCD.couter == _displayLCD.instantStatus[0])
        {
          _sensor.flagback = false;
          _displayLCD.type_infor = (e_statuslcd)_displayLCD.instantStatus[1];
        }
        else
        {
          _displayLCD.type_infor = escreenResult;
        }
        _displayLCD.changeScreen = true;
      }
      else if (_displayLCD.type_infor == ecalibSensor)
      {
        dbg_button("nut RED - bat dau calib");
        _sensor.flagCalibSensor = true;
      }
      else if (_displayLCD.type_infor == eprepare)
      {
        _displayLCD.couter = 0;
        _displayLCD.type_infor = ewaitingReadsensor;
        _displayLCD.changeScreen = true;
        dbg_button("nut RED - bat dau do");
      }
      else if (_displayLCD.type_infor == escreenAverageResult)
      {
        // _displayLCD.couter = 0;
        _displayLCD.type_infor = echooseSample;
        // _displayLCD.step = PC;
        _displayLCD.changeScreen = true;
        _displayLCD.flag_postData = true;
      }
      else if (_displayLCD.type_infor == e_setting ||
               _displayLCD.type_infor == echoosetube ||
               _displayLCD.type_infor == e_language ||
               _displayLCD.type_infor == THRESHOLD_SETTING_MANUAL ||
               _displayLCD.type_infor == echooseSample ||
               _displayLCD.type_infor == echooseAnimals)
      {
        _displayLCD.menu->moveDown();
      }
      else if (_displayLCD.type_infor == e_language)
      {
        _displayLCD.language = English;
        _displayLCD.changeScreen = true;
        _displayLCD.language_state = English;
        _displayLCD.type_infor = e_language;
      }
      else if (_displayLCD.type_infor == e_setThreshold)
      {
        _displayLCD.type_infor = THRESHOLD_SETTING_BLE;
        _displayLCD.changeScreen = true;
      }
      else if (_displayLCD.type_infor == THRESHOLD_SETTING_MANUAL)
      {
        _Threshold.isick = _displayLCD.menu->getSelectedIndex();
        _Threshold.address = ADDR_SAMPLE_DATA(animals_RD);
        _displayLCD.changeScreen = true;
      }
      else if (_displayLCD.type_infor == e_errorProcessing)
      {
        ESP.restart();
      }
      else if (_displayLCD.type_infor == e_updateThreshold)
      {
        _Threshold.decrementValue();
        _displayLCD.type_infor = e_updateThreshold;
        _displayLCD.changeScreen = true;
      }
      else if (_displayLCD.type_infor == e_loginKeyUpdateCalib)
      {
        _keyCalib.decrementValue();
        _displayLCD.type_infor = e_loginKeyUpdateCalib;
        _displayLCD.changeScreen = true;
      }
      break;
    }
    case B_GREEN:
    {
      if (_displayLCD.type_infor == escreenAverageResult)
      {
        _displayLCD.couter = 0;
        _displayLCD.changeScreen = true;
        _displayLCD.type_infor = eprepare;
        _sensor.clear();
        dbg_button("nut BLUE - do lai");
      }
      else if (_displayLCD.type_infor == escreenStart)
      {
        _displayLCD.type_infor = echooseSample;
        _displayLCD.changeScreen = true;
      }
      else if (_displayLCD.type_infor == ecalibSensor)
      {
        _displayLCD.type_infor = escreenStart;
        _displayLCD.changeScreen = true;
        dbg_button("nut BLUE - huy calib");
      }
      else if (_displayLCD.type_infor == echoosetube)
      {
        _sensor.sick = _displayLCD.menu->getSelectedIndex();
        measure_value = animals.items.listSick[_sensor.sick].sickName;
        _displayLCD.type_infor = eprepare;
        _displayLCD.changeScreen = true;
      }
      else if (_displayLCD.type_infor == echooseSample)
      {
        uint8_t tmp = _displayLCD.menu->getSelectedIndex();
        Sample_measure = animals.items.sampleNames[tmp];
        _displayLCD.type_infor = echoosetube;
        _displayLCD.changeScreen = true;
      }
      else if (_displayLCD.type_infor == echooseAnimals)
      {
        animals_RD = (group_type)_displayLCD.menu->getSelectedIndex();
        _displayLCD.type_infor = escreenStart;
        _displayLCD.changeScreen = true;
      }
      else if (_displayLCD.type_infor == e_setting)
      {
        uint8_t tmp = _displayLCD.menu->getSelectedIndex();
        switch (tmp)
        {
        case LANGUAGE: /* code */
        {
          _displayLCD.type_infor = e_language;
          break;
        }
        case SAMPLE:
        {
          _displayLCD.type_infor = echooseAnimals;
          break;
        }
        case WIFI:
        {
          _displayLCD.type_infor = e_settingWifi;
          break;
        }
        case THRESHOLD:
        {
          _displayLCD.type_infor = e_setThreshold;
          break;
        }
        case UPDATE:
        {
          _displayLCD.type_infor = e_settingUpdate;
          break;
        }
        }
        _displayLCD.changeScreen = true;
      }
      else if (_displayLCD.type_infor == e_language)
      {
        uint8_t tmp = _displayLCD.menu->getSelectedIndex();
        dbg_button("index: %d\r\n", tmp);
        _displayLCD.language = language_pointer(tmp);
        _displayLCD.type_infor = escreenStart;
        _displayLCD.changeScreen = true;
      }
      else if (_displayLCD.type_infor == e_setThreshold)
      {
        _displayLCD.type_infor = THRESHOLD_SETTING_MANUAL;
        _displayLCD.changeScreen = true;
      }
      else if (_displayLCD.type_infor == e_updateThreshold)
      {
        _Threshold.incrementValue();
        _displayLCD.type_infor = e_updateThreshold;
        _displayLCD.changeScreen = true;
      }
      else if (_displayLCD.type_infor == THRESHOLD_SETTING_MANUAL)
      {
        _Threshold.isick = _displayLCD.menu->getSelectedIndex();
        _Threshold.address = ADDR_SAMPLE_DATA(animals_RD);
        _displayLCD.type_infor = e_updateThreshold;
        _displayLCD.changeScreen = true;
      }
      else if (_displayLCD.type_infor == e_loginKeyUpdateCalib)
      {
        _keyCalib.incrementValue();
        _displayLCD.type_infor = e_loginKeyUpdateCalib;
        _displayLCD.changeScreen = true;
      }
      break;
    }
    case B_WHITE:
    {
      if (_displayLCD.type_infor == ecalibSensor)
      {
        _sensor.flagformatCalib = true;
        dbg_button("nut WHITE - format calib");
      }
      else if (_displayLCD.type_infor == e_setThreshold)
      {
        _displayLCD.type_infor = escreenStart;
        _displayLCD.changeScreen = true;
        _Threshold.clearThreshold();
      }
      else if (_displayLCD.type_infor == e_updateThreshold)
      {
        _Threshold.changeIndex();
        _displayLCD.type_infor = e_updateThreshold;
        _displayLCD.changeScreen = true;
      }
      else if (_displayLCD.type_infor == echoosetube ||
               _displayLCD.type_infor == echooseSample ||
               _displayLCD.type_infor == echooseAnimals ||
               _displayLCD.type_infor == e_setting ||
               _displayLCD.type_infor == e_language ||
               _displayLCD.type_infor == THRESHOLD_SETTING_MANUAL)
      {
        _displayLCD.menu->moveUp();
      }
      else if (_displayLCD.type_infor == e_loginKeyUpdateCalib)
      {
        _keyCalib.changeIndex();
        _displayLCD.type_infor = e_loginKeyUpdateCalib;
        _displayLCD.changeScreen = true;
      }
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
    _displayLCD.type_infor = e_loginCalib;
    _displayLCD.changeScreen = true;
    dbg_button("nut WHITE - calib");
  }
}
static void tickerHandler1(uint8_t index)
{
  buttonTicker[index].detach();
  if (!digitalRead(buttons[index]))
  {
    buttonPressed[index] = false;
    _displayLCD.type_infor = e_setting;
    _displayLCD.changeScreen = true;
    dbg_button("nut Red - setting");
  }
}

static void tickerHandlerSaveEeprom(uint8_t index)
{
  buttonTicker[index].detach();
  if (!digitalRead(buttons[index]))
  {
    buttonPressed[index] = false;
    _Threshold.saveThresholdtoEEPROM(ADDR_SAMPLE_LISTSICK(_Threshold.address, _Threshold.isick));
    animals.items.listSick[_Threshold.isick].threshold = _Threshold.getThresholdformEEPROM(ADDR_SAMPLE_LISTSICK(_Threshold.address, _Threshold.isick));
    // clear
    _Threshold.clearThreshold();
    delay(1000);
    _displayLCD.type_infor = THRESHOLD_SETTING_MANUAL;
    _displayLCD.changeScreen = true;
    dbg_button("nut WHITE - saving");
  }
}

static void tickerHandlerUpdateEeprom(uint8_t index)
{
  buttonTicker[index].detach();
  if (!digitalRead(buttons[index]))
  {
    buttonPressed[index] = false;
    ESP.restart();
  }
}

static void tickerHandlerEnterKeyCalib(uint8_t index)
{
  buttonTicker[index].detach();
  if (!digitalRead(buttons[index]))
  {
    buttonPressed[index] = false;
    uint16_t keyCalibEeprom;
    _keyCalib.saveThresholdtoEEPROM(ADDR_PASSWORD_CALIB);
    keyCalibEeprom = _keyCalib.getThresholdformEEPROM(ADDR_PASSWORD_CALIB);
    // clear
    _keyCalib.clearThreshold();
    if (keyCalibEeprom == 1225)
    {
      _displayLCD.type_infor = ecalibSensor;
    }
    else
    {
      _displayLCD.type_infor = escreenStart;
    }
    _displayLCD.changeScreen = true;
    delay(500);
  }
}

static void tickerHandlerGroupData(uint8_t index)
{
  buttonTicker[index].detach();
  if (!digitalRead(buttons[index]))
  {
    buttonPressed[index] = false;
    flag_groupData = true;
    flag_groupState ^= 1;
    _displayLCD.type_infor = echooseSample;
    _displayLCD.changeScreen = true;
    dbg_button("nut GREEN - bat dau grouping");
    // }
  }
}

void IRAM_ATTR buttonRedHandler()
{
  buttonProcess(B_RED);
}

void IRAM_ATTR buttonBlueHandler()
{
  buttonProcess(B_GREEN);
}

void IRAM_ATTR buttonWhiteHandler()
{
  buttonProcess(B_WHITE);
}

/* CODING */
SetManual::SetManual(/* args */)
{
}

SetManual::~SetManual()
{
}

void SetManual::saveThresholdtoEEPROM(uint16_t address)
{
  uint16_t tmp = 0;
  EEPROM.begin(_EEPROM_SIZE);
  // Write to EEPROM
  for (unsigned char i = 0; i < 4; i++)
  {
    tmp = tmp * 10 + this->value[i];
  }
  EEPROM.put(address, tmp);
  EEPROM.commit();
  EEPROM.end();
}

uint16_t SetManual::getThresholdformEEPROM(uint16_t address)
{
  uint16_t tmp = 0;
  EEPROM.begin(_EEPROM_SIZE);
  EEPROM.get(address, tmp);
  EEPROM.end();
  return tmp;
}

void SetManual::incrementValue(void)
{
  if (this->index == 105)
  {
    this->value[0]++;
    if (this->value[0] > 9)
    {
      this->value[0] = 0;
    }
  }
  else if (this->index == 145)
  {
    this->value[1]++;
    if (this->value[1] > 9)
    {
      this->value[1] = 0;
    }
  }
  else if (this->index == 185)
  {
    this->value[2]++;
    if (this->value[2] > 9)
    {
      this->value[2] = 0;
    }
  }
  else if (this->index == 225)
  {
    this->value[3]++;
    if (this->value[3] > 9)
    {
      this->value[3] = 0;
    }
  }
}

void SetManual::decrementValue(void)
{
  if (this->index == 105)
  {
    this->value[0]--;
    if (this->value[0] == 255)
    {
      this->value[0] = 9;
    }
  }
  else if (this->index == 145)
  {
    this->value[1]--;
    if (this->value[1] == 255)
    {
      this->value[1] = 9;
    }
  }
  else if (this->index == 185)
  {
    this->value[2]--;
    if (this->value[2] == 255)
    {
      this->value[2] = 9;
    }
  }
  else if (this->index == 225)
  {
    this->value[3]--;
    if (this->value[3] == 255)
    {
      this->value[3] = 9;
    }
  }
}

void SetManual::changeIndex(void)
{
  if (this->index == 225)
  {
    this->index = 65;
  }
  this->index += 40;
}

void SetManual::clearThreshold()
{
  this->index = 105;
  this->value[0] = 0;
  this->value[1] = 0;
  this->value[2] = 0;
  this->value[3] = 0;
}

buttonManager _buttonManager;
SetManual _Threshold = SetManual();
SetManual _keyCalib = SetManual(ADDR_PASSWORD_CALIB);
