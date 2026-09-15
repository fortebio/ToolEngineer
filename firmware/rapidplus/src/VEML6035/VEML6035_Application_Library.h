/*
 * VEML6035_Application_Library.h
 *
 * Created  : 14 December 2021
 * Modified : 15 March 2022
 * Author   : HWanyusof
 * Version	: 1.2
 */

#include "typedefinition.h"

/*Determine Resolution (in lux/count) From DG, ALS_IT, GAIN and SENS
 *VEML6035_CAL_Resolution(Byte DG, Byte ALS_IT, Byte GAIN, Byte SENS)
 *Byte DG - Input Parameter:
 *
 * VEML6035_DG_NORMAL
 * VEML6035_DG_DOUBLE
 *
 *Byte ALS_IT - Input Parameter:
 *
 * VEML6035_ALS_IT_25ms
 * VEML6035_ALS_IT_50ms
 * VEML6035_ALS_IT_100ms
 * VEML6035_ALS_IT_200ms
 * VEML6035_ALS_IT_400ms
 * VEML6035_ALS_IT_800ms
 *
 *Byte GAIN - Input Parameter:
 *
 * VEML6035_SENS_NORMAL
 * VEML6035_SENS_DOUBLE
 *
 *Byte SENS - Input Parameter:
 *
 * VEML6035_SENS_x1
 * VEML6035_SENS_x1_8
 *
 *Info: Refer to Resolution and Maximum Detection Range Table in Datasheet Page 9
 *
 *returns resolution (lux/count)
 */
float VEML6035_CAL_Resolution(Byte DG, Byte ALS_IT, Byte GAIN, Byte SENS);

/*Calculate the ALS Lux
 *VEML6035_CAL_Lux(float Resolution, float Count)
 *float Resolution - Output from VEML6035_CAL_Resolution(Byte DG, Byte ALS_IT, Byte GAIN, Byte SENS)
 *float Count - Output from VEML6035_GET_ALS_DATA()
 *
 *returns lux
 */
float VEML6035_CAL_Lux(float Resolution, float Count);

/*Get ALS_IT
 *VEML6035_GET_ALS_IT()
 *returns ALS_IT
 */
Byte VEML6035_GET_ALS_IT();

/*Get GAIN
 *VEML6035_GET_GAIN()
 *returns GAIN
 */
Byte VEML6035_GET_GAIN();

/*Get DG
 *VEML6035_GET_DG()
 *returns DG
 */
Byte VEML6035_GET_DG();

/*Get SENS
 *VEML6035_GET_SENS()
 *returns SENS
 */
Byte VEML6035_GET_SENS();

/*Get Delay for Measurement
 *VEML6035_GET_Delay()
 *returns delay in ms
 */
int VEML6035_GET_Delay();

/*Get ALS Mode
 *returns the ALS mode status of the sensor as follows:
 *
 * 0 - ALS Shutdown Mode
 * 1 - Auto Mode (Only ALS)
 * 2 - PSM with waiting time 0.4s (Only ALS)
 * 3 - PSM with waiting time 0.8s (Only ALS)
 * 4 - PSM with waiting time 1.6s (Only ALS)
 * 5 - PSM with waiting time 3.2s (Only ALS)
 * 6 - Auto Mode (ALS + White)
 * 7 - PSM with waiting time 0.4s (ALS + White)
 * 8 - PSM with waiting time 0.8s (ALS + White)
 * 9 - PSM with waiting time 1.6s (ALS + White)
 * 10 - PSM with waiting time 3.2s (ALS + White)
 */
int VEML6035_GET_ALS_Mode();

//Reset the Sensor to default Value
void Reset_Sensor();

//Print the output of the sensor
void Print_Data_Only();

//Print the output of ALS and white light
void ALS_White_Reading();

/*Print the variable in DEC for debugging
 *Print_Variable_DEC(Word Var)
 *Word Var - Input Parameter:
 *
 * Value between 0d0 and 0d65535
 */
void Print_Variable_DEC(Word Var);

/*Print the variable in HEX for debugging
 *Print_Variable_HEX(Word Var)
 *Word Var - Input Parameter:
 *
 * Value between 0d0 and 0d65535
 */
void Print_Variable_HEX(Word Var);

/*Reverses a string 'str' of length 'len'
 *reverse(char* str, int len)
 *char* str - Array pointer to be reversed
 *int len - Length of the array
 */
void reverse(char* str, int len);

/*Converts a given integer x to string str[].
 *intToStr(int x, char str[], int d)
 *int x - floating-point number to be converted to a string (Both the integer part as well as fraction/decimal point part)
 *char str[] - output string of the floating-point number in the form of array character
 *int d - number of decimal point (the integer part always = 0, the fraction/decimal point part = int afterpoint from ftoa() input)
 */
int intToStr(int x, char str[], int d);

/*Converts a floating-point/double number to string.
 *ftoa(float n, char* res, int afterpoint)
 *float n - floating-point number to be converted to a string
 *char* res - pointer to output string of the floating-point number in the form of array character
 *int d - number of decimal point
 */
void ftoa(float n, char* res, int afterpoint);
