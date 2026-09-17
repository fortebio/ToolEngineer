#include "button.h"

typedef void (*handler)();

void buttonRedHandler();
void buttonGreenHandler();
void buttonWhiteHandler();
void buttonProcess(e_statusbutton index);

static void tickerCalib(uint8_t index);
static void tickerSetting(uint8_t index);
static void tickerHandlerSaveEeprom(uint8_t index);
static void tickerHandlerUpdateEeprom(uint8_t index);

static uint8_t buttons[NumberButton];
static handler Handler[NumberButton];
static bool buttonPressed[NumberButton];
static Ticker buttonTicker[NumberButton];
static unsigned long timeAtPress[NumberButton];

buttonManager::buttonManager(/* args */)
{
  buttons[0] = BUTTON_RED;
  Handler[0] = &buttonRedHandler;
  buttons[1] = BUTTON_GREEN;
  Handler[1] = &buttonGreenHandler;
  buttons[2] = BUTTON_WHITE;
  Handler[2] = &buttonWhiteHandler;

  for (int i = 0; i < NumberButton; i++)
  {
    pinMode(buttons[i], INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(buttons[i]), Handler[i], CHANGE);
  }
}

buttonManager::~buttonManager()
{
  
}

void buttonProcess(e_statusbutton index)
{

  if (!digitalRead(buttons[index]) && (!buttonPressed[index]))
  {
    timeAtPress[index] = millis();
    if (index == B_WHITE)
    {
      buttonTicker[index].attach_ms(calibTime, &tickerCalib, (uint8_t)index);
    }
    if (index == B_RED)
    {
      buttonTicker[index].attach_ms(calibTime, &tickerSetting, (uint8_t)index);
    }
    if (index == B_GREEN)
    {
      if (_displayLCD.type_infor == e_updateThreshold)
      {
        buttonTicker[index].attach_ms(calibTime, &tickerHandlerSaveEeprom, (uint8_t)index);
      }
      else if (_displayLCD.type_infor == THRESHOLD_SETTING_MANUAL)
      {
        buttonTicker[index].attach_ms(calibTime, &tickerHandlerUpdateEeprom, (uint8_t)index);
      }
    }
    buttonPressed[index] = true;
  }
  else if ((buttonPressed[index]) && ((unsigned long)(millis() - timeAtPress[index]) > TimePressAnti) && ((unsigned long)(millis() - timeAtPress[index]) < calibTime))
  {
    buttonPressed[index] = false;
    switch (index)
    {
    case B_RED:
    {
      buttonTicker[index].detach();
      if (_sensor.flagback)
      {
        _displayLCD.counter++;
        if (_displayLCD.counter == _displayLCD.instantStatus[0])
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
          _sensor.flagCalibSensor = true;
      }
      else if (_displayLCD.type_infor == eprepare)
      {
        _displayLCD.counter = 0;
        _displayLCD.type_infor = ewaitingReadsensor;
        _displayLCD.changeScreen = true;
        dbg_button("nut RED - bat dau do");
      }
      ////////////////////////
      else if (_displayLCD.type_infor == escreenAverageResult)
      {
        //_sensor.clear();
        //_displayLCD.step_slot = 0;
        //_displayLCD.counter = 0;
        //_displayLCD.type_infor = escreenchooseSlot;
        //_displayLCD.step = PC;
        _displayLCD.type_infor = echooseSample;
        _displayLCD.changeScreen = true;
        _displayLCD.flag_postData = true;
      }
      else if (_displayLCD.type_infor == e_setting ||
               _displayLCD.type_infor == echooseTube ||
               _displayLCD.type_infor == e_language ||
               _displayLCD.type_infor == THRESHOLD_SETTING_MANUAL ||
               _displayLCD.type_infor == echooseSample)
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
      else if (_displayLCD.type_infor == echooseTube)
      {
        _displayLCD.type_infor = echooseTube;
        _displayLCD.changeScreen = true;
      }
      /*
      else if (_displayLCD.type_infor == escreenchooseSlot)
      {
        _displayLCD.type_infor = escreenchooseSlot;
        _displayLCD.changeScreen = true;
      }
      */ 
      else if (_displayLCD.type_infor == e_setThreshold)
      {
        _displayLCD.type_infor = THRESHOLD_SETTING_BLE;
        _displayLCD.changeScreen = true;
      }
      else if (_displayLCD.type_infor == THRESHOLD_SETTING_MANUAL)
      {
        uint8_t tmp = _displayLCD.menu->getSelectedIndex();
        _displayLCD.changeScreen = true;
        _Threshold.address = ADDR_THRESHOLD_BASE + 4 * tmp;
      }
      else if (_displayLCD.type_infor == e_updateThreshold)
      {
        dbg_display("ADDR_THRESHOLD: ");
        Serial.println(_Threshold.address);
        if (_Threshold.index == 105)
        {
          _Threshold.valueThreshold[0]--;
          if (_Threshold.valueThreshold[0] == 255)
          {
            _Threshold.valueThreshold[0] = 9;
          }
          _displayLCD.type_infor = e_updateThreshold;
          _displayLCD.changeScreen = true;
        }
        else if (_Threshold.index == 145)
        {
          _Threshold.valueThreshold[1]--;
          if (_Threshold.valueThreshold[1] == 255)
          {
            _Threshold.valueThreshold[1] = 9;
          }
          _displayLCD.type_infor = e_updateThreshold;
          _displayLCD.changeScreen = true;
        }
        else if (_Threshold.index == 185)
        {
          _Threshold.valueThreshold[2]--;
          if (_Threshold.valueThreshold[2] == 255)
          {
            _Threshold.valueThreshold[2] = 9;
          }
          _displayLCD.type_infor = e_updateThreshold;
          _displayLCD.changeScreen = true;
        }
        else if (_Threshold.index == 225)
        {
          _Threshold.valueThreshold[3]--;
          if (_Threshold.valueThreshold[3] == 255)
          {
            _Threshold.valueThreshold[3] = 9;
          }
          _displayLCD.type_infor = e_updateThreshold;
          _displayLCD.changeScreen = true;
        }
      }
      break;
    }
    case B_GREEN:
    {
      buttonTicker[index].detach();
      if (_displayLCD.type_infor == escreenAverageResult)
      {
        _displayLCD.counter = 0;
        _displayLCD.type_infor = eprepare;
        _displayLCD.changeScreen = true;
        _sensor.clear();
        dbg_button("nut BLUE - do lai");
      }
      else if (_displayLCD.type_infor == ecalibSensor)
      {
        _displayLCD.type_infor = escreenStart;
        _displayLCD.changeScreen = true;
        dbg_button("nut BLUE - huy calib");
      }
      else if (_displayLCD.type_infor == escreenStart)
      {
        _displayLCD.type_infor = echooseSample;
        //_displayLCD.type_infor = escreenchooseSlot;
        _displayLCD.changeScreen = true;
        dbg_button("nut BLUE - chuan bi");
      }
      /*
      else if (_displayLCD.type_infor == escreenchooseSlot)
      {
        _displayLCD.type_infor = echooseTube;
        _displayLCD.changeScreen = true;
      }
      */
      else if (_displayLCD.type_infor == echooseTube)
      {
        uint8_t tmp = _displayLCD.menu->getSelectedIndex();
        dbg_button("index: %d\r\n", tmp);
        switch (tmp)
        {
          case PC:
          {
            measure_value = "PC";
            _sensor.sick = PC;
            break;
          }
          case EHP:
          {
            measure_value = "EHP";
            _sensor.sick = EHP;
            break;
          }
          case EMS:
          {
            measure_value = "EMS";
            _sensor.sick = EMS;
            break;
          }
          case WSSV:
          {
            measure_value = "WSSV";
            _sensor.sick = WSSV;
            break;
          }
          case TPD:
          {
            measure_value = "TPD";
            _sensor.sick = TPD;
            break;
          }
        }
        // _displayLCD.couter = 1;
        _displayLCD.type_infor = eprepare;
        _displayLCD.changeScreen = true;
      }
      else if (_displayLCD.type_infor == echooseSample)
      {
        uint8_t tmp = _displayLCD.menu->getSelectedIndex();
        dbg_button("index: %d\r\n", tmp);
        switch (tmp)
        {
          case PRAWN_Vannamei:
          {
            Sample_measure = "PRAWN Vannamei";
            _sensor.sample = PRAWN_Vannamei;
            break;
          }
          case PRAWN_Monodon:
          {
            Sample_measure = "PRAWN Vannamei";
            _sensor.sample = PRAWN_Monodon;
            break;
          }
          case FISH_Tilapia:
          {
            Sample_measure = "FISH Tilapia";
            _sensor.sample = FISH_Tilapia;
            break;
          }
          case PIG:
          {
            Sample_measure = "PIG";
            _sensor.sample = PIG;
            break;
          }
          case WATER:
          {
            Sample_measure = "WATER";
            _sensor.sample = WATER;
            break;
          }
        }
        // _displayLCD.couter = 1;
        _displayLCD.type_infor = echooseTube;
        _displayLCD.changeScreen = true;
      }
      else if (_displayLCD.type_infor == e_setting)
      {
        uint8_t tmp = _displayLCD.menu->getSelectedIndex();
        dbg_button("index: %d\r\n", tmp);

        switch (tmp)
        {
          case LANGUAGE: /* code */
          {
            _displayLCD.type_infor = e_language;
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
        if (_Threshold.index == 105)
        {
          _Threshold.valueThreshold[0]++;
          if (_Threshold.valueThreshold[0] > 9)
          {
            _Threshold.valueThreshold[0] = 0;
          }
          _displayLCD.type_infor = e_updateThreshold;
          _displayLCD.changeScreen = true;
        }
        else if (_Threshold.index == 145)
        {
          _Threshold.valueThreshold[1]++;
          if (_Threshold.valueThreshold[1] > 9)
          {
            _Threshold.valueThreshold[1] = 0;
          }
          _displayLCD.type_infor = e_updateThreshold;
          _displayLCD.changeScreen = true;
        }
        else if (_Threshold.index == 185)
        {
          _Threshold.valueThreshold[2]++;
          if (_Threshold.valueThreshold[2] > 9)
          {
            _Threshold.valueThreshold[2] = 0;
          }
          _displayLCD.type_infor = e_updateThreshold;
          _displayLCD.changeScreen = true;
        }
        else if (_Threshold.index == 225)
        {
          _Threshold.valueThreshold[3]++;
          if (_Threshold.valueThreshold[3] > 9)
          {
            _Threshold.valueThreshold[3] = 0;
          }
          _displayLCD.type_infor = e_updateThreshold;
          _displayLCD.changeScreen = true;
        }
      }
      else if (_displayLCD.type_infor == THRESHOLD_SETTING_MANUAL)
      {
        uint8_t tmp = _displayLCD.menu->getSelectedIndex();
        _Threshold.address = ADDR_THRESHOLD_BASE + 4 * tmp;
        _displayLCD.type_infor = e_updateThreshold;
        _displayLCD.changeScreen = true;
      }
      break;
    }
    case B_WHITE:
    {
      buttonTicker[index].detach();
      if (_displayLCD.type_infor == ecalibSensor)
      {
        _sensor.flagformatCalib = true;
        dbg_button("nut WHITE - format calib");
      }

      else if (_displayLCD.type_infor == e_language)
      {
        _displayLCD.type_infor = escreenStart;
        _displayLCD.changeScreen = true;
      }
      else if (_displayLCD.type_infor == e_setThreshold)
      {
        _displayLCD.type_infor = escreenStart;
        _displayLCD.changeScreen = true;
        _Threshold.clearThreshold();
      }
      else if (_displayLCD.type_infor == e_updateThreshold)
      {
        if (_Threshold.index == 105)
        {
          _displayLCD.type_infor = e_updateThreshold;
          _displayLCD.changeScreen = true;
        }
        else if (_Threshold.index == 145)
        {
          _displayLCD.type_infor = e_updateThreshold;
          _displayLCD.changeScreen = true;
        }
        else if (_Threshold.index == 185)
        {
          _displayLCD.type_infor = e_updateThreshold;
          _displayLCD.changeScreen = true;
        }
        else if (_Threshold.index == 225)
        {
          _displayLCD.type_infor = e_updateThreshold;
          _displayLCD.changeScreen = true;
          _Threshold.index = 65;
        }
        _Threshold.index += 40;
      }
      else if (_displayLCD.type_infor == echooseTube ||
               _displayLCD.type_infor == echooseSample ||
               _displayLCD.type_infor == e_setting ||
               _displayLCD.type_infor == e_language ||
               _displayLCD.type_infor == THRESHOLD_SETTING_MANUAL

      )
      {
        _displayLCD.menu->moveUp();
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

static void tickerCalib(uint8_t index)
{
  buttonTicker[index].detach();
  if (!digitalRead(buttons[index]))
  {
    buttonPressed[index] = false;
    _sensor.slot = 0;
    _displayLCD.type_infor = ecalibSensor;
    _displayLCD.changeScreen = true;
    dbg_button("nut WHITE - calib");
  }
}

static void tickerSetting(uint8_t index)
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
    _Threshold.saveThresholdtoEEPROM(_Threshold.address);
    _Threshold.clearThreshold();
    delay(2000);
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

void IRAM_ATTR buttonRedHandler()
{
  buttonProcess(B_RED);
}

void IRAM_ATTR buttonGreenHandler()
{
  buttonProcess(B_GREEN);
}

void IRAM_ATTR buttonWhiteHandler()
{
  buttonProcess(B_WHITE);
}

Threshold::Threshold()
{
}

Threshold::~Threshold()
{
}

void Threshold::saveThresholdtoEEPROM(uint8_t address)
{
  uint16_t threshold = 0;
  EEPROM.begin(_EEPROM_SIZE);
  for (unsigned char i = 0; i < 4; i++)
  {
    threshold = threshold * 10 + _Threshold.valueThreshold[i];
  }
  _sensor.valueThreshold[((address - ADDR_THRESHOLD_BASE) / 4)] = threshold;
  Serial.println(_Threshold.address);
  Serial.println(_sensor.valueThreshold[((address - ADDR_THRESHOLD_BASE) / 4)]);

  EEPROM.put(address, threshold);
  EEPROM.commit();
  EEPROM.end();
}

void Threshold::clearThreshold()
{
  _Threshold.index = 105;
  _Threshold.valueThreshold[0] = 0;
  _Threshold.valueThreshold[1] = 0;
  _Threshold.valueThreshold[2] = 0;
  _Threshold.valueThreshold[3] = 0;
}

buttonManager _buttonManager;
Threshold _Threshold;
