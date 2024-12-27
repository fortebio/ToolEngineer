/*
 * I2C_Functions.h
 *
 * Created  : 14 December 2021
 * Modified : 15 March 2022
 * Author   : HWanyusof
 * Version	: 1.2
 */
 
#pragma once 

#include <stdio.h>
#include <stdlib.h>
#include "typedefinition.h"

//Arduino platforms use Wire.h library while Teensy platforms use i2c_t3.h library.
#ifdef i2ct3
	#include <i2c_t3.h>
#endif

#ifdef wirelib
	#include <Arduino.h>
	#include <Wire.h>
#endif

//Struct TransferData Member Definition
struct TransferData
{
	uint8_t RegisterAddress;
	uint8_t WData[2];
	uint8_t RData[2];
	uint8_t length;
	uint8_t Slave_Address;
	uint8_t Select_I2C_Bus;
}; //Struct variables will be declared separately in Sensor API and I2C_Functions.cpp/c

//Function Prototypes For I2C_Functions.cpp/c
int ReadI2C_Bus(struct TransferData *Data);
int WriteI2C_Bus(struct TransferData *Data);
