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

buttonManager _buttonManager;

WebServer server(80);

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
void setup()
{
  Serial.setRxBufferSize(3 * 1024);
  Serial.begin(115200);
  // configure the I2C IO
  Wire.begin(SDA_Forte, SCL_Forte);

  /***** Create Mutex *****/
  gI2CMutex = xSemaphoreCreateMutex();

  if (gI2CMutex == NULL)
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

  WiFi.begin(ssid.c_str(), password.c_str());
  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 20)
  {
    delay(50); // đợi 50ms mỗi lần
    retries++;
    Serial.print(".");
  }
  delay(100);

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

  // postData_Chart();
  checkFirmware();

  /****** Create RTOS Tasks *****/

  xTaskCreatePinnedToCore(
      ControlTask,
      "ControlTask",
      8192,
      NULL,
      5,
      &controlTaskHandle,
      1);

  xTaskCreatePinnedToCore(
      SensorTask,
      "SensorTask",
      16384,
      NULL,
      2,
      &sensorTaskHandle,
      1);

  xTaskCreatePinnedToCore(
      DisplayTask,
      "DisplayTask",
      16384,
      NULL,
      2,
      &displayTaskHandle,
      0);

  xTaskCreatePinnedToCore(
      NetworkTask,
      "NetworkTask",
      8192,
      NULL,
      1,
      &networkTaskHandle,
      0);

  xTaskCreatePinnedToCore(
      InputTask,
      "InputTask",
      8192,
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
}
