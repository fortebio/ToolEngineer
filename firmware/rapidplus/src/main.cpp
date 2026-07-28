/*
Version 1.3 note: add function send wifi id and password via bluetooth
Version 1.4 note: add function sellect language
*/

#include "define.h"
#include "displayCLD.h"
#include "button.h"
#include "Bluetooth.h"
#include "thermometer.h"
#include "Wire.h"
#include "sensor6035.h"
#include "PIDControl.h"
#include "ForteSetting.h"
#include "Fan.h"
#include "errorCheck.h"
#include "webDashboard.h"
#include "wifiStore.h" // saved networks (NVS) tried at boot by connectSavedNetworks()

buttonManager _buttonManager;

/****** RTOS Handles ******/
TaskHandle_t displayTaskHandle = NULL;
TaskHandle_t networkTaskHandle = NULL;
TaskHandle_t sensorTaskHandle = NULL;
TaskHandle_t controlTaskHandle = NULL;
TaskHandle_t inputTaskHandle = NULL;
TaskHandle_t settingTaskHandle = NULL;

/***** Global Mutex ******/
SemaphoreHandle_t gI2CMutex = NULL;
SemaphoreHandle_t gSPIMutex = NULL;
// Serializes every EEPROM.begin..end section. The single global Arduino EEPROM object
// shares ONE 4096B heap buffer that begin() reallocs and end() frees, so two tasks
// overlapping their begin..end double-free it (GOTCHA 2). Take/give via eepromLock/Unlock.
SemaphoreHandle_t gEepromMutex = NULL;

/***** DisplayTask ******/
/***********************************************************************
 * Function: DisplayTask()
 * Description: FreeRTOS task (pinned to core 0) that drives the TFT UI by
 *  calling _displayCLD.loop() every 100 ms via vTaskDelayUntil for a fixed
 *  refresh cadence.
 * pramameter: pvParameters - FreeRTOS task parameter pointer (unused)
 *  return: none (runs forever in an infinite loop)
 */
void DisplayTask(void *pvParameters)
{
  TickType_t xLastWakeTime = xTaskGetTickCount();

  while (1)
  {
    _displayCLD.loop();

    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(100));
  }
}

/***** NetworkTask ******/
/***********************************************************************
 * Function: NetworkTask()
 * Description: FreeRTOS task (pinned to core 0) that services the OTA
 *  firmware-update flow by polling updateFirmware() every 10 ms; the
 *  actual download only proceeds once the user has accepted the update.
 * pramameter: pvParameters - FreeRTOS task parameter pointer (unused)
 *  return: none (runs forever in an infinite loop)
 */
void NetworkTask(void *pvParameters)
{
  while (1)
  {
    updateFirmware();
    dashboardLoop(); // push the "home" SSE event (self-throttled to 1/s)

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

/***** SensorTask *****/
/***********************************************************************
 * Function: SensorTask()
 * Description: FreeRTOS task (pinned to core 1) that reads the AS6035 opto
 *  sensor by calling _sensor6035.loop() every 20 ms, guarding the I2C bus
 *  with gI2CMutex (50 ms timeout) so it does not collide with other I2C
 *  users.
 * pramameter: pvParameters - FreeRTOS task parameter pointer (unused)
 *  return: none (runs forever in an infinite loop)
 */
void SensorTask(void *pvParameters)
{
  TickType_t xLastWakeTime = xTaskGetTickCount();
  while (1)
  {
    if (xSemaphoreTake(gI2CMutex, pdMS_TO_TICKS(50)) == pdTRUE)
    {
      _sensor6035.loop();
      xSemaphoreGive(gI2CMutex);
    }
    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(20));
  }
}

/***** ControlTask *****/
/***********************************************************************
 * Function: ControlTask()
 * Description: FreeRTOS task (pinned to core 1) that runs the temperature
 *  control loop, calling _PIDControl.loop() and _Fan.loop() every 100 ms to
 *  drive the PID heater regulation and keep the fan running.
 * pramameter: pvParameters - FreeRTOS task parameter pointer (unused)
 *  return: none (runs forever in an infinite loop)
 */
void ControlTask(void *pvParameters)
{
  TickType_t xLastWakeTime = xTaskGetTickCount();
  while (1)
  {
    _PIDControl.loop();
    _Fan.loop(); // keep the Fan on (trivial GPIO write, folded in here)

    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(100));
  }
}

/***** InputTask *****/
// Button polling + buzzer timing. Both need fast, regular servicing.
// Button handlers drive the TFT heavily, but every display call takes the
// recursive gSPIMutex internally (SPILock), so running concurrently with
// DisplayTask is safe.
/***********************************************************************
 * Function: InputTask()
 * Description: FreeRTOS task (pinned to core 1) that polls user input and
 *  audio feedback every 5 ms, calling _buttonManager.loop() for button
 *  handling and _buzzer.loop() for buzzer timing; the fast period ensures
 *  responsive button servicing.
 * pramameter: pvParameters - FreeRTOS task parameter pointer (unused)
 *  return: none (runs forever in an infinite loop)
 */
void InputTask(void *pvParameters)
{
  TickType_t xLastWakeTime = xTaskGetTickCount();
  while (1)
  {
    _buttonManager.loop();
    _buzzer.loop();

    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(5));
  }
}

/***** SettingTask *****/
// Serial JSON config receiver. Isolated because its loop() busy-waits up to
// ~30ms while draining the serial buffer; keeping it off InputTask avoids
// delaying button response.
/***********************************************************************
 * Function: SettingTask()
 * Description: FreeRTOS task (pinned to core 0) that handles serial JSON
 *  configuration input, calling _ForteSetting.loop() every 10 ms; isolated
 *  from InputTask because its loop can busy-wait while draining the serial
 *  buffer.
 * pramameter: pvParameters - FreeRTOS task parameter pointer (unused)
 *  return: none (runs forever in an infinite loop)
 */
void SettingTask(void *pvParameters)
{
  while (1)
  {
    _ForteSetting.loop();

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

/***********************************************************************
 * Function: setup()
 * Description: Arduino startup routine that initializes the serial port and
 *  I2C bus, creates the I2C mutex and recursive SPI mutex (halting on
 *  failure), starts the buttons, loads device settings and the error log
 *  from EEPROM, connects to WiFi (up to 20 retries), brings up the display,
 *  ForteSetting, PID control, AS6035 sensor and fan, runs the OTA firmware
 *  check, then creates the six pinned FreeRTOS tasks (Control, Sensor,
 *  Display, Network, Input, Setting) across both cores.
 * pramameter: none
 *  return: none
 */
// Boot-time WiFi bring-up across the preferred + saved networks. Blocks, but early-exits
// the moment a network connects, so a present network is joined in 1-3 s and only a real
// outage waits out the timeouts. This is the ONLY place WiFi.begin() may be called -
// runtime begin() deadlocks async_tcp (test_no_runtime_wifi_begin.py).
static bool wifiTryOne(const String &s, const String &p, uint32_t timeoutMs)
{
  if (!s.length())
    return false;
  Serial.printf("[wifi] trying '%s'\n", s.c_str());
  WiFi.begin(s.c_str(), p.c_str());
  uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs)
  {
    if (WiFi.status() == WL_CONNECTED)
    {
      Serial.printf("[wifi] connected to '%s' -> %s\n", s.c_str(),
                    WiFi.localIP().toString().c_str());
      return true;
    }
    delay(100); // yields; feeds the idle-task watchdog during the wait
  }
  return false;
}

static void connectSavedNetworks()
{
  // A save/connect from the web parks its credentials as a TRIAL (never commits in place).
  // Test it FIRST here. On success COMMIT it (becomes the preferred network + list front);
  // on failure DISCARD it, leave the previous network untouched, and record the failure so
  // the web can say "wrong password - re-enter". This is what stops a typo from wiping the
  // working network. Password verification requires a real association, hence a reboot.
  String tSsid, tPass;
  if (wifiStoreGetTrial(tSsid, tPass))
  {
    Serial.printf("[wifi] testing new credentials for '%s'\n", tSsid.c_str());
    if (wifiTryOne(tSsid, tPass, 8000)) // longer window: auth + DHCP for a brand-new net
    {
      ssid = tSsid;
      password = tPass;
      saveSettingDevice();        // commit as the preferred pair (EEPROM)
      wifiStoreAdd(tSsid, tPass); // and to the front of the saved list
      wifiStoreClearTrial();
      wifiStoreSetTrialResult(WIFI_TRIAL_OK, tSsid);
      Serial.printf("[wifi] '%s' verified and committed\n", tSsid.c_str());
      return; // already connected
    }
    // Distinguish "not found" (moved out of range) from an auth failure (wrong password),
    // so the web can say the right thing - Connect on a known-good network that is briefly
    // out of range must NOT be reported as a wrong password.
    const char *reason =
        (WiFi.status() == WL_NO_SSID_AVAIL) ? "range" : "auth";
    Serial.printf("[wifi] '%s' FAILED (%s) -> keeping old network\n", tSsid.c_str(), reason);
    wifiStoreClearTrial();
    wifiStoreSetTrialResult(WIFI_TRIAL_FAILED, tSsid, reason);
    // fall through: connect the previous network (EEPROM globals unchanged)
  }

  // Preferred first (the EEPROM pair): give it the longer window since a fresh DHCP
  // lease routinely needs 1-3 s (GOTCHA 5). Common case: connects in 1-3 s and returns
  // here, so boot is NOT held up - only a real outage waits out the timeouts.
  if (wifiTryOne(ssid, password, 4000))
    return;

  // Fallback across the saved list, tightly time-boxed: this only matters when the
  // preferred network is absent, and every second here is a second of dark boot screen.
  // Cap the whole loop at 5 s (was 9 s) so worst-case boot with NO network in range is
  // ~9 s (4 s preferred + 5 s here) before dashboardLoop raises the SoftAP.
  WifiNet nets[WIFI_STORE_MAX];
  uint8_t n = wifiStoreLoad(nets, WIFI_STORE_MAX);
  uint32_t budget0 = millis();
  for (uint8_t i = 0; i < n; i++)
  {
    if (nets[i].ssid == ssid)
      continue; // already tried as the preferred one
    if (millis() - budget0 > 5000)
      break;
    if (wifiTryOne(nets[i].ssid, nets[i].pass, 2500))
      return;
  }
  Serial.println("[wifi] no saved network joined at boot -> grace, then SoftAP");
}

void setup()
{
  Serial.setRxBufferSize(3 * 1024);
  Serial.begin(115200);
  // configure the I2C IO
  Wire.begin(SDA_Forte, SCL_Forte);

  /***** Create Mutex *****/
  gI2CMutex = xSemaphoreCreateMutex();
  // Created HERE, before any xTaskCreate below, so the first EEPROM section on any task
  // already sees a valid handle (eepromLock null-guards the pre-creation boot reads too).
  gEepromMutex = xSemaphoreCreateMutex();

  if (gI2CMutex == NULL || gEepromMutex == NULL)
  {
    Serial.println("Create I2C Mutex Failed");

    while (1)
    {
      delay(1000);
    }
  }

  gSPIMutex = xSemaphoreCreateRecursiveMutex();

  if (gSPIMutex == NULL)
  {
    Serial.println("Create SPI Mutex Failed");
    while (1)
    {
      delay(1000);
    }
  }

  // configure the button
  _buttonManager.buttonStart();

  // load ssid, password, id_device id from EEPROM
  loadSettingDevice();
  error.readErrorFromEEPROM(); // read error record from EEPROM, used for error process

  // Release the Bluetooth (BTDM) memory HERE, before WiFi/lwIP allocate, instead of later
  // in dashboardBegin(). BT is never used in this build (no SerialBT.begin at boot; the web
  // Setting tab replaced BT config), so esp_bt_mem_release hands its ~60KB to the heap. Doing
  // it FIRST lets the allocator place WiFi/AsyncWebServer/TLS around the full region, aiming
  // for a larger contiguous internal block (intLargest) so the mbedTLS handshake (~42KB) fits
  // on heap-tight boards -> fewer -32512 at upload. Idempotent (gBtReleased guard).
  dashHeapProbe("before BT release");
  releaseBluetoothStack();
  dashHeapProbe("after BT release");

  WiFi.mode(WIFI_STA);
  // Register a stable hostname with DHCP (must be after mode, before begin). Routers that
  // resolve DHCP hostnames then reach the device at http://<hostname>/, and it matches the
  // mDNS name (http://<hostname>.local/) - so the dashboard has a fixed name when the IP moves.
  WiFi.setHostname(dashboardHostname().c_str());
  // Power save OFF. The ESP32 defaults to WIFI_PS_MIN_MODEM, which parks the radio
  // between DTIM beacons: 100-300 ms latency spikes and dropped packets. That is
  // invisible for a one-shot request but wrecks a 1 s SSE stream + AsyncWebServer -
  // the dashboard stutters and requests time out. Costs a few mA on a mains device.
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  // Try the preferred network (EEPROM) then each SAVED network (/wifi.json), so the
  // machine can be moved between rooms without reconfiguring. ALL WiFi.begin() lives in
  // setup() on purpose: a runtime begin() from a task deadlocks async_tcp forever
  // (test_no_runtime_wifi_begin.py). Bounded early-exit blocking: returns the instant one
  // network connects, so a present network is NOT delayed; only a full outage waits.
  connectSavedNetworks();
  dashHeapProbe("after WiFi connect");

  // If none connected, WiFi is left in STA-disconnected: dashboardLoop() (NetworkTask)
  // gives it a grace window and then raises the SoftAP fallback, without blocking setup().

  _displayCLD.begin();
  _ForteSetting.begin();
  _PIDControl.begin();
  _displayCLD.logoFortebiotech();

  /***** Sensor Init *****/
  if (xSemaphoreTake(gI2CMutex, pdMS_TO_TICKS(500)) == pdTRUE)
  {
    _sensor6035.begin();

    xSemaphoreGive(gI2CMutex);
  }

  _Fan.begin();
  _PIDControl.timeoutSetting();

  checkFirmware();

  /****** Create RTOS Tasks *****/

  xTaskCreatePinnedToCore(
      ControlTask,
      "ControlTask",
      4096,
      NULL,
      5,
      &controlTaskHandle,
      1);

  xTaskCreatePinnedToCore(
      SensorTask,
      "SensorTask",
      10240,
      NULL,
      2,
      &sensorTaskHandle,
      1);

  xTaskCreatePinnedToCore(
      DisplayTask,
      "DisplayTask",
      10240,
      NULL,
      2,
      &displayTaskHandle,
      0);

  xTaskCreatePinnedToCore(
      NetworkTask,
      "NetworkTask",
      6144,
      NULL,
      1,
      &networkTaskHandle,
      0);

  xTaskCreatePinnedToCore(
      InputTask,
      "InputTask",
      6144,
      NULL,
      3,
      &inputTaskHandle,
      1);

  xTaskCreatePinnedToCore(
      SettingTask,
      "SettingTask",
      8192,
      NULL,
      1,
      &settingTaskHandle,
      0);
  dashHeapProbe("end of setup (tasks up)");
}

/***********************************************************************
 * Function: loop()
 * Description: Arduino main loop task; all real work has been moved into the
 *  dedicated FreeRTOS tasks created in setup(), so this simply idles with a
 *  1000 ms vTaskDelay each iteration.
 * pramameter: none
 *  return: none
 */
void loop()
{
  // All work has been moved into dedicated RTOS tasks created in setup():
  //   - ControlTask : _PIDControl.loop() + _Fan.loop()
  //   - SensorTask  : _sensor6035.loop()
  //   - DisplayTask : _displayCLD.loop()
  //   - InputTask   : _buttonManager.loop() + _buzzer.loop()
  //   - SettingTask : _ForteSetting.loop()
  //   - NetworkTask : updateFirmware()
  // Nothing left to do on the Arduino loop task; just idle.
  vTaskDelay(pdMS_TO_TICKS(1000));

  // ponytail: temporary stack census. 8192+16384+16384+8192+8192+8192 = 65 536 B of task
  // stacks, never measured - and the upload dies for want of ~20 KB. uxTaskGetStackHighWaterMark
  // returns the smallest free stack (BYTES on ESP32) each task has ever had, so `used` below is
  // its true peak. Whatever is unused here is RAM the upload could have had. Let it run through
  // one full run + upload first: mbedTLS does its handshake on DisplayTask's stack, so that
  // peak only shows up after a real upload. Delete this once the sizes are cut.
  static uint8_t tick = 0;
  if (++tick >= 10)
  {
    tick = 0;
    struct
    {
      const char *name;
      TaskHandle_t h;
      uint32_t size;
    } t[] = {
        {"Control", controlTaskHandle, 4096},
        {"Sensor", sensorTaskHandle, 10240},
        {"Display", displayTaskHandle, 10240},
        {"Network", networkTaskHandle, 6144},
        {"Input", inputTaskHandle, 6144},
        {"Setting", settingTaskHandle, 8192},
    };
    uint32_t waste = 0;
    Serial.print("[stack]");
    for (auto &e : t)
    {
      if (!e.h)
        continue;
      uint32_t freeB = uxTaskGetStackHighWaterMark(e.h);
      Serial.printf(" %s=%u/%u", e.name, (unsigned)(e.size - freeB), (unsigned)e.size);
      waste += freeB;
    }
    Serial.printf(" | unused=%u B\n", (unsigned)waste);
  }
}
