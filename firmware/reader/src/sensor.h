#ifndef _SENNSOR_H
#define _SENNSOR_H

#include "define.h"
#include "displayLCD.h"
#include "button.h"

#define numSensor 1
#define numSampling 3
#define numSample 3
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

public:
  sensor(/* args */);
  ~sensor();

  void begin();
  void read_Sensor(uint8_t num);
  bool read_Allsensor();
  void handle_Sensor();
  uint16_t calib_SensorLight();
  void format_CalibSensor();
  void Average_Result();
  void clear();
  void loop();

  float Data_Sensor1[numSample];

  uint8_t typecalib = 0;
  uint32_t Calib_Sensor[2];
  uint16_t valueThreshold[5] = {0, 0, 0, 0, 0};
  uint16_t valueCalibMax;
  uint16_t valueCalibMin;
  uint8_t sick;
  // sample_type sample;

  uint32_t result_Sensor[numSensor][numSampling];
  uint32_t AverageResult[numSensor];
  uint32_t raw_Sensor[numSensor];
  float sum_Sensor[numSensor];
  uint32_t sensor_kalman[numSensor];
  bool sensorError[numSampling] = {true, true, true};

  bool flagReadSensor = false;
  bool flagCalibSensor = false;
  bool flagformatCalib = false;
  bool flagback = false;
};

extern sensor _sensor;
#endif
