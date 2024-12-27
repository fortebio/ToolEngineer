#include "button.h"
#include "Ticker.h"
#include "displayCLD.h"
#include "PIDControl.h"
// #include "sensor.h"
#include "Bluetooth.h"
typedef void (*hanler)();
void buttonRedHandler();
void buttonBlueHandler();
void buttonWhiteHandler();
// static void tickerHandler(uint8_t index);
// static void tickerHandler1(uint8_t index);
// static void tickerHandler2(uint8_t index);

static uint8_t buttons[NumberButton];
static hanler Hanler[NumberButton];
static bool buttonPressed[NumberButton];
static Ticker buttonTicker[NumberButton];
static unsigned long timeAtPress[NumberButton];

buttonManager::buttonManager(/* args */) {
  buttons[0] = BUTTON_RED;
  Hanler[0] = &buttonRedHandler;
  buttons[1] = BUTTON_BLUE;
  Hanler[1] = &buttonBlueHandler;
  buttons[2] = BUTTON_WHITE;
  Hanler[2] = &buttonWhiteHandler;
}

void buttonManager::buttonStart() {
  for (int i = 0; i < NumberButton; i++) {
    pinMode(buttons[i], INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(buttons[i]), Hanler[i], CHANGE);
  }
}

buttonManager::~buttonManager() {
}

void buttonProcess(e_statusbutton index) {

  if (!digitalRead(buttons[index]) && (buttonPressed[index] == false)) {
    timeAtPress[index] = millis();
    // if (index == B_WHITE) {
    //   buttonTicker[index].attach_ms(calibTime, &tickerHandler, (uint8_t)index);
    // }
    // if (index == B_RED) {
    //   buttonTicker[index].attach_ms(calibTime, &tickerHandler1, (uint8_t)index);
    // }
    buttonPressed[index] = true;
  }

  else if ((buttonPressed[index] == true) && ((unsigned long)(millis() - timeAtPress[index]) > TimePressAnti) && ((unsigned long)(millis() - timeAtPress[index]) < calibTime)) {
    
    _buzzer.BuzzerStop();       //stop the buzzer if any button is pressed
    
    
    buttonPressed[index] = false;
    switch (index) {
      case B_RED:
        {
          if (_displayCLD.ErrorStatus())
          {
            return;
          }
          /*if (_sensor.flagback == true) {
            _displayCLD.couter++;
            if (_displayCLD.couter == _displayCLD.instantStatus[0]) {
              _sensor.flagback = false;
              _displayCLD.type_infor = (e_statuslcd)_displayCLD.instantStatus[1];
            } else {
              _displayCLD.type_infor = escreenResult;
            }
            _displayCLD.changeScreen = true;
          }

          else if (_displayCLD.type_infor == ecalibSensor) {
            dbg_button("nut RED - bat dau calib");
            _sensor.flagCalibSensor = true;
          }
          //added function to start the lysis heating after red button is pressed
          else */if (_displayCLD.type_infor == ewaitLysisTube)
          {
            _displayCLD.type_infor = eheatLysis;
            _displayCLD.changeScreen = true;
            // _displayCLD.timeRefresh = 0;
            _displayCLD.startHeating10mins();
            dbg_button("Red Btn - start heating lysis");
          }
          
          else if (_displayCLD.type_infor == escreenStart) {    //skip to amplification stage directly if pressing red at the beginning
            _displayCLD.type_infor = epreheating67;
            _displayCLD.bheadershow = true;
            _displayCLD.changeScreen = true;
            _PIDControl.setPreheat67();        //check the current temperature is not over heat
            _sensor6035.setStepeSensorpreheat();
            dbg_button("red button - start amplification");
          }

          else if (_displayCLD.type_infor == ewaitampTube)
          {
            _displayCLD.type_infor = eoptoreading;
            _displayCLD.changeScreen = true;
            // _displayCLD.timeRefresh = 0;
            _displayCLD.startAmplification();
            dbg_button("Red Btn - start amplification");
          }
          
          else if (_displayCLD.type_infor == eprepare) {
            _displayCLD.type_infor = ewaitingReadsensor;
            _displayCLD.changeScreen = true;
            dbg_button("nut RED - bat dau do");
          }
/*
          else if (_displayCLD.type_infor == escreenResult && _displayCLD.couter < 3) {
            _displayCLD.couter++;
            _displayCLD.type_infor = eprepare;
            _displayCLD.changeScreen = true;
            dbg_button("nut RED - tiep tuc do");
          }

          else if (_displayCLD.type_infor == escreenResult && _displayCLD.couter >= 3) {
            _displayCLD.couter = 0;
            _displayCLD.type_infor = escreenAverageResult;
            _displayCLD.changeScreen = true;
            dbg_button("nut RED - ket qua tb 3 lan do");
          }
          else if(_displayCLD.type_infor ==e_setting)
          {
            _displayCLD.type_infor=e_language;
            _displayCLD.changeScreen=true;
          }
          else if(_displayCLD.type_infor ==e_language)
          {
            _displayCLD.language =1;
            _displayCLD.changeScreen=true;
            _displayCLD.language_state=English;
            _displayCLD.type_infor =e_language;
          
          }
*/
          //add new function @ 20240111 by dxdhub
          // else if (_displayCLD.type_infor == epreheating67)
          // {
          //   /* code */
          //   //start to maintain the 67 degree and keep measuring the sensor
          //   _displayCLD.type_infor=eoptoreading;
          //   _displayCLD.changeScreen=true;
          // }         
/*

          else if(_displayCLD.type_infor == echoosetube)
          {
            
            if(_displayCLD.step==1)
            {
              _displayCLD.type_infor=echoosetube;
              _displayCLD.changeScreen = true;
              //_displayCLD.step=2;
            }
            else if(_displayCLD.step==2)
            {
              _displayCLD.type_infor=echoosetube;
              _displayCLD.changeScreen = true;
              //_displayCLD.step=3;
            }
            else if(_displayCLD.step==3)
            {
              _displayCLD.type_infor=echoosetube;
              _displayCLD.changeScreen = true;
              //_displayCLD.step=4;
            }
            else if(_displayCLD.step==4)
            {
              _displayCLD.type_infor=echoosetube;
              _displayCLD.changeScreen = true;
             // _displayCLD.step=5;
            }
            else if(_displayCLD.step ==5)
            {
              _displayCLD.type_infor=echoosetube;
              _displayCLD.changeScreen = true;
              _displayCLD.step=0;
            }
            _displayCLD.step++;
          }  
*/
          
          break;
         
        }

      case B_BLUE:
        {
          if (_displayCLD.ErrorStatus())
          {
            return;
          }
/*          if (_displayCLD.type_infor == escreenAverageResult) {
            _displayCLD.type_infor = escreenStart;
            _displayCLD.changeScreen = true;
          }

          else if (_displayCLD.type_infor == ecalibSensor) {
            _displayCLD.type_infor = escreenStart;
            _displayCLD.changeScreen = true;
            dbg_button("nut BLUE - huy calib");
          }

          else*/ //if (_displayCLD.type_infor == escreenResult) {
            // _displayCLD.type_infor = eprepare;
            // _displayCLD.changeScreen = true;
            // _displayCLD.startHeating10mins();
            // dbg_button("nut BLUE - do lai");
          // }

          /*else*/ if (_displayCLD.type_infor == escreenStart) {    //start to heat up to 80 degree
            //_displayCLD.couter = 1;
            _displayCLD.type_infor = epreheating80;     //actually should start from 80 degree
            _displayCLD.changeScreen = true;
            _displayCLD.bheadershow = true;
            _PIDControl.setpid1startpreHeat80();        //check the current temperature is not over heat
            dbg_button("green button - start heating to 80");
          }

          else if (_displayCLD.type_infor == ewaitphase2)
          {
            _displayCLD.type_infor = epreheating67;
            _displayCLD.bheadershow = true;
            _displayCLD.changeScreen = true;
            _PIDControl.setPreheat67();        //check the current temperature is not over heat
            _sensor6035.setStepeSensorpreheat();
            dbg_button("green button - start heating to 67");
          }

          else if (_displayCLD.type_infor == epreheating67)
          {
            _sensor6035.skip2Maintain();
            dbg_button("green button - skip opto preheat");
          }
          

          // else if (_displayCLD.type_infor == escreenStart) {
          //   //_displayCLD.couter = 1;
          //   _displayCLD.type_infor = echoosetube;
          //   _displayCLD.changeScreen = true;
          //   dbg_button("nut BLUE - chuan bi");
          
          // }
          
/*          else if (_displayCLD.type_infor == echoosetube) {
            _displayCLD.couter = 1;
            _displayCLD.type_infor = eprepare;
            _displayCLD.changeScreen = true;
          }
           else if(_displayCLD.type_infor ==e_setting)
          {
          _displayCLD.type_infor = e_connect_bluetooth;
          _displayCLD.changeScreen =true;
          }
          else if(_displayCLD.type_infor ==e_language)
          {
            _displayCLD.language =0;
            _displayCLD.changeScreen=true;
            _displayCLD.language_state=VietNamese ;
            _displayCLD.type_infor =e_language;
            
          }
*/          break;
        }

      case B_WHITE:
        {
          if (_displayCLD.FinishStatus())    //button pressed when display the result or error status, then return to start
          {
            // _displayCLD.type_infor = errprocess;
            _displayCLD.type_infor = escreenRestart;
            _displayCLD.changeScreen = true;
            // ESP.restart();
            // _ForteSetting.rerun();
            return;
          }
          if (_displayCLD.ErrorStatus())
          {
            // _displayCLD.type_infor = escreenRestart;
            // _displayCLD.type_infor = eErrResart;
            // _displayCLD.changeScreen = true;
            // ESP.restart();
            // _ForteSetting.rerun();
            return;
          }
          if(_displayCLD.type_infor == escreenStart)    //no need to restart as at the start screen already
          {
            return;
          }
          _displayCLD.type_infor = ebuttonrestart;
          _displayCLD.changeScreen = true;
          // ESP.restart();
          // _ForteSetting.rerun();
          return;
          
/*          buttonTicker[index].detach();
          if (_displayCLD.type_infor == ecalibSensor) {
            _sensor.flagformatCalib = true;
            dbg_button("nut WHITE - format calib");
          }

          else if (_displayCLD.type_infor == escreenResult || _displayCLD.type_infor == eprepare)  //|| _displayCLD.type_infor == escreenAverageResult
          {
            if (_sensor.flagback == false && _displayCLD.couter > 1) {
              _sensor.flagback = true;
              _displayCLD.instantStatus[0] = _displayCLD.couter;
              _displayCLD.instantStatus[1] = _displayCLD.type_infor;
            }
            if (_displayCLD.couter > 1) {
              _displayCLD.couter--;
              _displayCLD.type_infor = escreenResult;
              _displayCLD.changeScreen = true;
            }
          }
           else if(_displayCLD.type_infor ==escreenAverageResult)
          {
            _displayCLD.type_infor=logdata;
            _displayCLD.changeScreen=true;
          }
           else if(_displayCLD.type_infor ==e_language)
          {
            _displayCLD.type_infor=escreenStart;
            _displayCLD.changeScreen=true;
          }
*/
          break;
        }

      default:
        break;
    }
  } else {
    buttonPressed[index] = false;
  }
}

// static void tickerHandler(uint8_t index) {
//   buttonTicker[index].detach();

//   if (!digitalRead(buttons[index])) {
//     buttonPressed[index] = false;
//     _displayCLD.type_infor = ecalibSensor;
//     _displayCLD.changeScreen = true;
//     dbg_button("nut WHITE - calib");
//   }
// }
// static void tickerHandler1(uint8_t index) {
//   buttonTicker[index].detach();
// if (!digitalRead(buttons[index])) {
//     buttonPressed[index] = false;
//     _displayCLD.type_infor = e_setting;
//     _displayCLD.changeScreen = true;
//     dbg_button("nut Red - setting");
//   }
// }


void IRAM_ATTR buttonRedHandler() {
  buttonProcess(B_RED);
}

void IRAM_ATTR buttonBlueHandler() {
  buttonProcess(B_BLUE);
}

void IRAM_ATTR buttonWhiteHandler() {
  buttonProcess(B_WHITE);
}

// buttonManager _buttonManager;
