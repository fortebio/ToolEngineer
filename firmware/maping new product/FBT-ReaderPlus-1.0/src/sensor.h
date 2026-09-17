#ifndef _SENSOR_H_
#define _SENSOR_H_

#include "define.h"
#include "displayLCD.h"
#include "button.h"
#include "TCA9548.h"
#include "displayresources.h"
#include <EEPROM.h>

#define numSensor 4
#define numSampling 3
#define numSample 3
#define numSick 5
#define PWM_channel 0

#define highest_calib 16
#define lowest_calib 0

/* POSITIVE THRESHOLD */
extern uint16_t Threshold_PO;
extern uint16_t Threshold_NE;
extern uint16_t Threshold_EHP;
extern uint16_t Threshold_EMS;
extern uint16_t Threshold_WSSV;

class sensor {
private:
	uint8_t I2C_Channel[4] = {0, 1, 2, 3};
	TCA9548 I2CMux;
	uint8_t iChannel = 0;
  uint8_t PWM_LED[4] = {127, 127, 127, 127};
  const int ledPins[4] = {LED_1, LED_2, LED_3, LED_4};
public:
  sensor(/* args */);
  ~sensor();

  void begin();
  void LED_on(uint8_t channel);
  void LED_off(uint8_t channel);
  void LED_off_All();
  void read_Sensor(uint8_t channel);
  void read_All_Sensor();
  void handle_Sensor(uint8_t channel);
  void handle_All_Sensor();
  uint16_t calib_Sensor(uint8_t channel);
  void format_CalibSensor(uint8_t address);
  void format_All_CalibSensor();
  void Average_Result(uint8_t channel);
  void Average_All_Result();
  void clear();
  void loop();

  float Data_Sensor[numSensor][numSample];

  uint8_t typecalib = 0;
  uint8_t counter_calib = 0;
  uint8_t slot = 0;
  uint16_t valueThreshold[numSick] = {0, 0, 0, 0, 0};
  uint16_t valueCalibMax[numSensor] = {0, 0, 0, 0};
  uint16_t valueCalibMin[numSensor] = {0, 0, 0, 0};
  sick_type sick;
  sample_type sample;
  uint32_t value_sensor[numSensor];
  uint32_t result_Sensor[numSensor][numSampling];
  uint32_t AverageResult[numSensor];
  float sum_Sensor[numSensor];
  uint32_t sensor_kalman[numSensor];
  uint32_t nValue[numSensor];

  float valueCalib[numSensor];

  bool flagReadSensor = false;
  bool flagCalibSensor = false;
  bool flagformatCalib = false;
  bool flagback = false;
};

extern sensor _sensor;
#endif