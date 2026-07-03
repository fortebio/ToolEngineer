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
/***********************************************************************
 * Function: isrRed()
 * Description: IRAM-resident ISR for the RED button GPIO CHANGE edge.
 * Reads the RED pin (active-low) into btnState[B_RED].rawPressed and
 * records the edge timestamp via millis(). Does no business logic.
 * pramameter: none
 *  return: none
 */
static void IRAM_ATTR isrRed()
{
  btnState[B_RED].rawPressed = !digitalRead(buttonPins[B_RED]);
  btnState[B_RED].lastEdgeTime = millis(); // millis() is ISR-safe on ESP32
}

/***********************************************************************
 * Function: isrBlue()
 * Description: IRAM-resident ISR for the BLUE button GPIO CHANGE edge.
 * Reads the BLUE pin (active-low) into btnState[B_BLUE].rawPressed and
 * records the edge timestamp via millis(). Does no business logic.
 * pramameter: none
 *  return: none
 */
static void IRAM_ATTR isrBlue()
{
  btnState[B_BLUE].rawPressed = !digitalRead(buttonPins[B_BLUE]);
  btnState[B_BLUE].lastEdgeTime = millis();
}

/***********************************************************************
 * Function: isrWhite()
 * Description: IRAM-resident ISR for the WHITE button GPIO CHANGE edge.
 * Reads the WHITE pin (active-low) into btnState[B_WHITE].rawPressed and
 * records the edge timestamp via millis(). Does no business logic.
 * pramameter: none
 *  return: none
 */
static void IRAM_ATTR isrWhite()
{
  btnState[B_WHITE].rawPressed = !digitalRead(buttonPins[B_WHITE]);
  btnState[B_WHITE].lastEdgeTime = millis();
}

// ================================================================
// Constructor / Destructor
// ================================================================
/***********************************************************************
 * Function: buttonManager()
 * Description: Constructor. Zero-initializes the shared btnState array for
 * all NumberButton buttons (rawPressed, lastEdgeTime, debounced,
 * debounceTime, longPressFired) and clears each pendingEvent to
 * BTN_EVENT_NONE.
 * pramameter: none
 *  return: none
 */
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

// ================================================================
// buttonStart() — attach ISR on CHANGE edge
// ================================================================
/***********************************************************************
 * Function: buttonStart()
 * Description: Configures all button GPIOs as INPUT_PULLUP and attaches the
 * isrRed/isrBlue/isrWhite handlers on the CHANGE edge so press/release
 * transitions are captured.
 * pramameter: none
 *  return: none
 */
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
/***********************************************************************
 * Function: pollButton()
 * Description: Runs the per-button debounce and long-press state machine
 * from the raw ISR-captured state. Debounces a new press over TimePressAnti
 * ms, then while held fires BTN_EVENT_LONG_PRESS once after longPressMs, or
 * on release fires BTN_EVENT_SHORT_PRESS if no long-press occurred. Sets
 * the button's pendingEvent for later dispatch.
 * pramameter: index = button index (B_RED/B_BLUE/B_WHITE) to poll;
 * pramameter: longPressMs = hold threshold in ms for this button's long press
 *  return: none
 */
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
/***********************************************************************
 * Function: processEvent()
 * Description: Dispatches a pending button event to its handler. For a
 * short press it stops the buzzer then calls the per-button
 * handleShortPress_Red/Blue/White; for a long press it calls the
 * per-button handleLongPress_Red/Blue/White.
 * pramameter: index = which button (B_RED/B_BLUE/B_WHITE) the event belongs to;
 * pramameter: event = the event type (BTN_EVENT_SHORT_PRESS/BTN_EVENT_LONG_PRESS)
 *  return: none
 */
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
/***********************************************************************
 * Function: loop()
 * Description: Main loop for buttonManager. Polls button states, applies
 * debounce and long-press detection, and dispatches events to handlers.
 * This replaces the original ISR-based buttonProcess() and tickerHandler()
 * pramameter: none
 *  return: none
 */
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
/***********************************************************************
 * Function: handleShortPress_Red()
 * Description: Handles a RED short press as a context-sensitive "confirm/
 * advance" action driven by _displayCLD.type_infor. Returns immediately on
 * an error screen. Depending on the current screen it: starts the 10-min
 * lysis heating (ewaitLysisTube -> eheatLysis), starts amplification preheat
 * to 67C (escreenStart -> epreheating67), readies PID23 then moves to the
 * wait-amp screen (eSelectAmpli -> ewaitampTube), starts amplification
 * (ewaitampTube -> eoptoreading), enters upload-data (eSettingMenu ->
 * eUpLoadData), cycles the selected slot 0..9 (eSelectSlot), advances mode
 * to set-LED-power (eSelectMode/eCalibComplete -> eSetPowerLed), increments
 * the digit of the currently indexed LED power 0..9 (eSetPowerLed), accepts
 * an OTA update (eUpdateOTA -> OTA_USER_ACCEPTED), or shows the error-result
 * screen from result/review/finished/upload screens. Sets changeScreen where
 * a redraw is needed.
 * pramameter: none
 *  return: none
 */
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
    _displayCLD.type_infor = eheating67;
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
  else if (_displayCLD.type_infor == ecalibPreheatStart)
  {
    // calib p0: user pressed RED -> start preheating heater2,3 to 55C
    _PIDControl.setCalibPreheat55();
    _displayCLD.type_infor = ecalibPreheating;
    _displayCLD.bheadershow = true;
    _displayCLD.changeScreen = true;
    dbg_button("red button - calib preheat 55C start");
  }
  else if (_displayCLD.type_infor == ecalibSelect)
  {
    // calib p1: user chose Amplification -> heat to 65/75 with a 5-minute hold
    _PIDControl.hotlidWaitMs = 5 * 60000;
    _PIDControl.timeStartWait = millis();
    _PIDControl.setPreheat67();
    _sensor6035.setStepeSensorpreheat();
    _displayCLD.type_infor = eheating67;
    _displayCLD.bheadershow = true;
    _displayCLD.changeScreen = true;
    dbg_button("red button - calib -> amplification");
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
/***********************************************************************
 * Function: handleShortPress_Blue()
 * Description: Handles a BLUE short press as a context-sensitive "secondary/
 * navigate" action driven by _displayCLD.type_infor. Returns immediately on
 * an error screen. Depending on the current screen it: starts preheat to 80C
 * (escreenStart -> epreheating80), starts preheat to 67C from phase-2 wait
 * (ewaitphase2 -> epreheating67), skips the opto preheat to maintain
 * (epreheating67), enters WiFi settings (eSettingMenu -> eSettingWifi),
 * navigates the ampli/slot/mode menus (eSelectAmpli -> eSelectSlot ->
 * eSelectMode -> eCalibrating), starts sensor calibration (eCalibrating),
 * advances the edited LED-power index 0..2 (eSetPowerLed), re-enters
 * calibration from eCalibComplete, returns to slot select from save-power/
 * save-calib screens, or dismisses an OTA update (eUpdateOTA ->
 * OTA_DISMISSED, back to escreenStart). Sets changeScreen where a redraw is
 * needed.
 * pramameter: none
 *  return: none
 */
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
    _PIDControl.timeStartWait = 0; // millis() - (10 * 60000);
    _displayCLD.type_infor = eheating67;
    _displayCLD.bheadershow = true;
    _displayCLD.changeScreen = true;
    _PIDControl.setPreheat67();
    _sensor6035.setStepeSensorpreheat();
    dbg_button("green button - start heating to 67");
  }
  else if (_displayCLD.type_infor == ecalibSelect)
  {
    // calib p1: user chose Calibration -> enter the existing calib menu while
    // heater2,3 keep maintaining 55C (pidStep stays epidcalibmaintain55).
    _sensor6035.setStepeSensorwait();
    _displayCLD.type_infor = eSelectSlot;
    _displayCLD.changeScreen = true;
    dbg_button("green button - calib -> select slot");
  }
  else if (_displayCLD.type_infor == epreheat67)
  {
    // _PIDControl.timeStartWait = 0;
    _displayCLD.type_infor = ewaitampTube;
    _displayCLD.changeScreen = true;
    _PIDControl.setPID23Ready();

    // _sensor6035.skip2Maintain();
    // _PIDControl.timeStartWait = 0;
    // _displayCLD.bheadershow = true;
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
/***********************************************************************
 * Function: handleShortPress_White()
 * Description: Handles a WHITE short press as a context-sensitive "back/
 * cancel/restart" action driven by _displayCLD.type_infor. On a finished
 * status it goes to the restart-confirm screen (escreenRestart); on an error
 * screen it does nothing. Otherwise: enters Bluetooth settings (eSettingMenu
 * -> eSettingBluetooth), saves LED power (eSetPowerLed -> eSavePowerLed),
 * goes from mode select to slot select (eSelectMode -> eSelectSlot), reboots
 * the device via ESP.restart() on the slot screen (eSelectSlot), does nothing
 * on the start screen, and otherwise shows the button-restart screen
 * (ebuttonrestart). Sets changeScreen where a redraw is needed.
 * pramameter: none
 *  return: none
 */
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
/***********************************************************************
 * Function: handleLongPress_Red()
 * Description: Handles a RED long press by switching the display to the
 * Setting menu (type_infor = eSettingMenu) and requesting a screen redraw.
 * pramameter: none
 *  return: none
 */
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
/***********************************************************************
 * Function: handleLongPress_Blue()
 * Description: Handles a BLUE long press by setting the sensor step to wait and
 * opening the calib-preheat entry (type_infor = ecalibPreheatStart): a prompt to
 * press RED to preheat heater2,3 to 55C before choosing Calib or Amplification.
 * pramameter: none
 *  return: none
 */
void buttonManager::handleLongPress_Blue()
{
  // Calib flow p0: prompt to preheat heater2,3 to 55C before calib/amp.
  // Heaters stay off until the user presses RED (handleShortPress_Red).
  _sensor6035.setStepeSensorwait();
  _displayCLD.type_infor = ecalibPreheatStart;
  _displayCLD.bheadershow = true;
  _displayCLD.changeScreen = true;
  info_display("Blue long-press - calib preheat 55C");
}

// ----------------------------------------------------------------
// WHITE long-press (5s) → Review
// Original: tickerHandler()
// ----------------------------------------------------------------
/***********************************************************************
 * Function: handleLongPress_White()
 * Description: Handles a WHITE long press by switching the display to the
 * review screen (type_infor = escreenReview) and requesting a screen redraw.
 * pramameter: none
 *  return: none
 */
void buttonManager::handleLongPress_White()
{
  _displayCLD.changeScreen = true;
  _displayCLD.type_infor = escreenReview;
  dbg_button("White long-press - review");
}
