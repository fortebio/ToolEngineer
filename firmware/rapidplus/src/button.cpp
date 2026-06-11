/*
 * Architecture:
 *   ISR (IRAM_ATTR) — records raw GPIO edge into volatile ButtonState
 *   loop()          — polls ButtonState, applies debounce + long-press detection,
 *                     fires business logic in safe task context
 *
 * Why this matters:
 *   Original code ran buttonProcess() (hundreds of µs, I2C, Serial printf)
 *   directly inside GPIO ISR. This caused:
 *   - Watchdog timeout / crash
 *   - Race conditions on _displayCLD, _PIDControl, _sensor6035 shared state
 *   - I2C bus corruption from mcp.digitalWrite() in ISR
 *
 * Change summary:
 *   - ISR: ~5 instructions (read GPIO, set flag, record timestamp)
 *   - Ticker long-press: replaced by polling in loop() (eliminates timer ISR too)
 *   - All business logic: runs in Arduino loop() context (Core 1, P1)
 */

#include "button.h"
#include "displayCLD.h"
#include "PIDControl.h"

// ================================================================
// GPIO pin array
// ================================================================
static const uint8_t buttonPins[NumberButton] = {
    BUTTON_RED,  // B_RED   = 0
    BUTTON_BLUE, // B_BLUE  = 1
    BUTTON_WHITE // B_WHITE = 2
};

// ================================================================
// Shared state between ISR and loop()
// Only rawPressed and lastEdgeTime are written by ISR.
// Everything else is written only by loop().
// ================================================================
static volatile ButtonState btnState[NumberButton];

// ================================================================
// ISR handlers — MINIMAL: read pin, record edge, exit
//
// IRAM_ATTR ensures code is in internal RAM (required for ESP32 ISR)
// No Serial, no I2C, no FreeRTOS API calls, no function calls
// beyond digitalRead.
// ================================================================
static void IRAM_ATTR isrRed()
{
  btnState[B_RED].rawPressed = !digitalRead(buttonPins[B_RED]);
  btnState[B_RED].lastEdgeTime = millis(); // millis() is ISR-safe on ESP32
}

static void IRAM_ATTR isrBlue()
{
  btnState[B_BLUE].rawPressed = !digitalRead(buttonPins[B_BLUE]);
  btnState[B_BLUE].lastEdgeTime = millis();
}

static void IRAM_ATTR isrWhite()
{
  btnState[B_WHITE].rawPressed = !digitalRead(buttonPins[B_WHITE]);
  btnState[B_WHITE].lastEdgeTime = millis();
}

// ================================================================
// Constructor / Destructor
// ================================================================
buttonManager::buttonManager()
{
  // Zero-initialize all button states
  for (int i = 0; i < NumberButton; i++)
  {
    btnState[i].rawPressed = false;
    btnState[i].lastEdgeTime = 0;
    btnState[i].debounced = false;
    btnState[i].debounceTime = 0;
    btnState[i].longPressFired = false;
    btnState[i].pendingEvent = BTN_EVENT_NONE;
  }
}

buttonManager::~buttonManager()
{
}

// ================================================================
// buttonStart() — attach ISR on CHANGE edge
// ================================================================
void buttonManager::buttonStart()
{
  for (int i = 0; i < NumberButton; i++)
  {
    pinMode(buttonPins[i], INPUT_PULLUP);
  }
  attachInterrupt(digitalPinToInterrupt(buttonPins[B_RED]), isrRed, CHANGE);
  attachInterrupt(digitalPinToInterrupt(buttonPins[B_BLUE]), isrBlue, CHANGE);
  attachInterrupt(digitalPinToInterrupt(buttonPins[B_WHITE]), isrWhite, CHANGE);
}

// ================================================================
// pollButton() — debounce + long-press detection (called from loop)
//
// State machine per button:
//   IDLE (debounced=false)
//     → rawPressed=true for >TimePressAnti ms → debounced=true, start hold timer
//   HELD (debounced=true)
//     → hold duration >= longPressMs → fire LONG_PRESS event (once)
//     → rawPressed=false (released) → fire SHORT_PRESS event (if no long-press fired)
// ================================================================
void buttonManager::pollButton(uint8_t index, uint16_t longPressMs)
{
  // Read volatile state atomically (single-byte reads are atomic on ESP32)
  bool currentRaw = btnState[index].rawPressed;
  uint32_t now = millis();

  if (!btnState[index].debounced)
  {
    // Currently not pressed — check for new press
    if (currentRaw)
    {
      // Debounce: raw must stay pressed for TimePressAnti ms
      if (btnState[index].debounceTime == 0)
      {
        // First detection of press
        btnState[index].debounceTime = now;
      }
      else if ((now - btnState[index].debounceTime) >= TimePressAnti)
      {
        // Debounce passed — transition to HELD
        btnState[index].debounced = true;
        btnState[index].longPressFired = false;
      }
    }
    else
    {
      // Noise — reset debounce timer
      btnState[index].debounceTime = 0;
    }
  }
  else
  {
    // Currently held — check for long-press or release
    uint32_t holdDuration = now - btnState[index].debounceTime;

    if (currentRaw)
    {
      // Still held — check long-press threshold
      if (!btnState[index].longPressFired && holdDuration >= longPressMs)
      {
        btnState[index].longPressFired = true;
        btnState[index].pendingEvent = BTN_EVENT_LONG_PRESS;
      }
    }
    else
    {
      // Released — fire short press only if long-press didn't fire
      if (!btnState[index].longPressFired && holdDuration >= TimePressAnti)
      {
        btnState[index].pendingEvent = BTN_EVENT_SHORT_PRESS;
      }
      // Reset state for next press
      btnState[index].debounced = false;
      btnState[index].debounceTime = 0;
    }
  }
}

// ================================================================
// processEvent() — dispatch pending events
// ================================================================
void buttonManager::processEvent(e_statusbutton index, e_buttonEvent event)
{
  if (event == BTN_EVENT_SHORT_PRESS)
  {
    // Stop buzzer on any button press (matches original behavior)
    _buzzer.BuzzerStop();

    switch (index)
    {
    case B_RED:
      handleShortPress_Red();
      break;
    case B_BLUE:
      handleShortPress_Blue();
      break;
    case B_WHITE:
      handleShortPress_White();
      break;
    }
  }
  else if (event == BTN_EVENT_LONG_PRESS)
  {
    switch (index)
    {
    case B_RED:
      handleLongPress_Red();
      break;
    case B_BLUE:
      handleLongPress_Blue();
      break;
    case B_WHITE:
      handleLongPress_White();
      break;
    }
  }
}

// ================================================================
// loop() — call from Arduino loop(), every 1ms
// ================================================================
void buttonManager::loop()
{
  // Poll all 3 buttons with their respective long-press thresholds
  pollButton(B_RED, LONG_PRESS_RED_MS);
  pollButton(B_BLUE, LONG_PRESS_BLUE_MS);
  pollButton(B_WHITE, LONG_PRESS_WHITE_MS);

  // Manual display recovery: hold BLUE + WHITE together for ~1.5s.
  // Fires once, then waits for both to be released before allowing again.
  // This only requests a TFT panel re-init — type_infor, timers, sensor step
  // are untouched, so the run continues from the same step after the redraw.
  //
  // Robustness:
  //   - longPressFired is forced true the moment both are detected pressed
  //     (BEFORE the 1.5s threshold), so an accidental brief dual-press never
  //     leaks into single-button short-press handlers (WHITE short-press
  //     would otherwise restart the device while a run is active).
  //   - Up to 150ms of "one button momentarily not seen as pressed" is
  //     tolerated as switch chatter so the timer doesn't reset spuriously.
  static uint32_t chordStart = 0;
  static uint32_t lastBothSeen = 0;
  static bool chordFired = false;
  bool bluePressed = btnState[B_BLUE].rawPressed;
  bool whitePressed = btnState[B_WHITE].rawPressed;
  uint32_t nowMs = millis();
  bool bothPressed = bluePressed && whitePressed;

  if (bothPressed)
  {
    lastBothSeen = nowMs;
    // Suppress single-button events on both buttons IMMEDIATELY — not
    // just when chord finally fires — so even a sub-threshold dual press
    // doesn't trigger WHITE short-press restart on release.
    btnState[B_BLUE].longPressFired = true;
    btnState[B_WHITE].longPressFired = true;
  }

  bool chordActive = bothPressed ||
                     (chordStart != 0 && (nowMs - lastBothSeen) <= 150);

  if (chordActive)
  {
    if (chordStart == 0)
      chordStart = nowMs == 0 ? 1 : nowMs;
    if (!chordFired && (nowMs - chordStart) >= 1500)
    {
      chordFired = true;
      _displayCLD.requestReinit = true;
      btnState[B_BLUE].pendingEvent = BTN_EVENT_NONE;
      btnState[B_WHITE].pendingEvent = BTN_EVENT_NONE;
      Serial.println("Display reinit triggered by BLUE+WHITE chord");
    }
  }
  else
  {
    chordStart = 0;
    // Require both released for ≥200ms before allowing another chord —
    // avoids re-fire if a single button bounces back on after the chord.
    if (!bluePressed && !whitePressed &&
        (nowMs - lastBothSeen) > 200)
    {
      chordFired = false;
    }
  }

  // Process any pending events
  for (int i = 0; i < NumberButton; i++)
  {
    e_buttonEvent evt = btnState[i].pendingEvent;
    if (evt != BTN_EVENT_NONE)
    {
      btnState[i].pendingEvent = BTN_EVENT_NONE; // Consume event
      processEvent((e_statusbutton)i, evt);
    }
  }
}

// ================================================================
// ================================================================
//
//  BUSINESS LOGIC — identical to original button.cpp
//  Only change: runs in loop() context instead of ISR
//
// ================================================================
// ================================================================

// ----------------------------------------------------------------
// RED short press — original: case B_RED in buttonProcess()
// ----------------------------------------------------------------
void buttonManager::handleShortPress_Red()
{
  if (_displayCLD.ErrorStatus())
  {
    return;
  }

  if (_displayCLD.type_infor == ewaitLysisTube)
  {
    _displayCLD.type_infor = eheatLysis;
    _displayCLD.changeScreen = true;
    _displayCLD.startHeating10mins();
    dbg_button("Red Btn - start heating lysis");
  }
  else if (_displayCLD.type_infor == escreenStart)
  {
    // Skip to amplification stage directly
    // _PIDControl.waitWarmAmpTube = true; // reset the flag in case user press red button to start heating but then change their mind and press blue button to skip preheat
    _PIDControl.timeStartWait = millis();
    _displayCLD.type_infor = epreheating67;
    _displayCLD.bheadershow = true;
    _displayCLD.changeScreen = true;
    _PIDControl.setPreheat67();
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
      _displayCLD.led_power[0]++;
      if (_displayCLD.led_power[0] > 9)
        _displayCLD.led_power[0] = 0;
      break;
    case 1:
      _displayCLD.led_power[1]++;
      if (_displayCLD.led_power[1] > 9)
        _displayCLD.led_power[1] = 0;
      break;
    case 2:
      _displayCLD.led_power[2]++;
      if (_displayCLD.led_power[2] > 9)
        _displayCLD.led_power[2] = 0;
      break;
    default:
      break;
    }
    _displayCLD.type_infor = eSetPowerLed;
    _displayCLD.changeScreen = true;
  }
  else if (_displayCLD.type_infor == eUpdateOTA)
  {
    // User accepted the update. NetworkTask::updateFirmware() will pick
    // this up on its next tick and transition to OTA_UPDATING.
    otaState = OTA_USER_ACCEPTED;
  }
  else if (_displayCLD.type_infor == escreenResult ||
           _displayCLD.type_infor == escreenReview ||
           _displayCLD.type_infor == escreenFinished ||
           _displayCLD.type_infor == eUpLoadData)
  {
    _displayCLD.type_infor = escreenErrorResult;
    _displayCLD.changeScreen = true;
  }
}

// ----------------------------------------------------------------
// BLUE short press — original: case B_BLUE in buttonProcess()
// ----------------------------------------------------------------
void buttonManager::handleShortPress_Blue()
{
  if (_displayCLD.ErrorStatus())
  {
    return;
  }

  if (_displayCLD.type_infor == escreenStart)
  {
    _displayCLD.type_infor = epreheating80;
    _displayCLD.changeScreen = true;
    _displayCLD.bheadershow = true;
    _PIDControl.setpid1startpreHeat80();
    dbg_button("green button - start heating to 80");
  }
  else if (_displayCLD.type_infor == ewaitphase2)
  {
    // _PIDControl.waitWarmAmpTube = true; // reset the flag in case user press red button to start heating but then change their mind and press blue button to skip preheat
    _PIDControl.timeStartWait = 0;
    _displayCLD.type_infor = epreheating67;
    _displayCLD.bheadershow = true;
    _displayCLD.changeScreen = true;
    _PIDControl.setPreheat67();
    _sensor6035.setStepeSensorpreheat();
    dbg_button("green button - start heating to 67");
  }
  else if (_displayCLD.type_infor == epreheating67)
  {
    _sensor6035.skip2Maintain();
    dbg_button("green button - skip opto preheat");
  }
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
    _displayCLD.changeScreen = true;
  }
  else if (_displayCLD.type_infor == eSaveCalib)
  {
    _displayCLD.type_infor = eSelectSlot;
    _displayCLD.changeScreen = true;
    _displayCLD.flag_calib_done = false;
  }
  else if (_displayCLD.type_infor == eUpdateOTA)
  {
    // User dismissed the update prompt — don't re-prompt until reboot.
    otaState = OTA_DISMISSED;
    _displayCLD.type_infor = escreenStart;
    _displayCLD.changeScreen = true;
  }
}

// ----------------------------------------------------------------
// WHITE short press — original: case B_WHITE in buttonProcess()
// ----------------------------------------------------------------
void buttonManager::handleShortPress_White()
{
  if (_displayCLD.FinishStatus())
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
    _displayCLD.changeScreen = true;
    return;
  }
  if (_displayCLD.type_infor == eSetPowerLed)
  {
    _displayCLD.type_infor = eSavePowerLed;
    _displayCLD.changeScreen = true;
    return;
  }
  if (_displayCLD.type_infor == eSelectMode)
  {
    _displayCLD.type_infor = eSelectSlot;
    _displayCLD.changeScreen = true;
    return;
  }
  if (_displayCLD.type_infor == eSelectSlot)
  {
    ESP.restart();
  }
  if (_displayCLD.type_infor == escreenStart)
  {
    return; // Already at start screen
  }

  _displayCLD.type_infor = ebuttonrestart;
  _displayCLD.changeScreen = true;
}

// ================================================================
// LONG-PRESS handlers
// Original: tickerHandler1 (RED), tickerHandler2 (BLUE), tickerHandler (WHITE)
// ================================================================

// ----------------------------------------------------------------
// RED long-press (3s) → Setting menu
// Original: tickerHandler1()
// ----------------------------------------------------------------
void buttonManager::handleLongPress_Red()
{
  _displayCLD.type_infor = eSettingMenu;
  _displayCLD.changeScreen = true;
  dbg_button("Red long-press - setting");
}

// ----------------------------------------------------------------
// BLUE long-press (3s) → Calibration
// Original: tickerHandler2()
// ----------------------------------------------------------------
void buttonManager::handleLongPress_Blue()
{
  _sensor6035.setStepeSensorwait();
  _displayCLD.type_infor = eSelectAmpli;
  _displayCLD.changeScreen = true;
  info_display("Blue long-press - calibrating");
}

// ----------------------------------------------------------------
// WHITE long-press (5s) → Review
// Original: tickerHandler()
// ----------------------------------------------------------------
void buttonManager::handleLongPress_White()
{
  _displayCLD.changeScreen = true;
  _displayCLD.type_infor = escreenReview;
  dbg_button("White long-press - review");
}
