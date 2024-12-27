/*
 * VEML6035_Application_Library.cpp
 *
 * Created  : 14 December 2021
 * Modified : 15 March 2022
 * Author   : HWanyusof
 * Version	: 1.2
 */

#include "VEML6035_Prototypes.h"
#include "VEML6035.h"
#include "I2C_Functions.h"
#include "VEML6035_Application_Library.h"

extern int I2C_Bus;

//****************************************************************************************************
//***************************************Application API**********************************************


/*Determine Resolution (in lux/count) From DG, ALS_IT, GAIN and SENS
 *VEML6035_CAL_Resolution(Byte DG, Byte ALS_IT, Byte GAIN, Byte SENS)
 *Byte DG - Input Parameter:
 *
 * VEML6035_DG_0_NORMAL
 * VEML6035_DG_1_DOUBLE
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
 * VEML6035_GAIN_0_NORMAL
 * VEML6035_GAIN_1_DOUBLE
 *
 *Byte SENS - Input Parameter:
 *
 * VEML6035_SENS_0_x1
 * VEML6035_SENS_1_x1_8
 *
 *Info: Refer to Resolution and Maximum Detection Range Table in Datasheet Page 9
 *
 *returns resolution (lux/count)
 */
float VEML6035_CAL_Resolution(Byte DG, Byte ALS_IT, Byte GAIN, Byte SENS)
{
    float Resolution;
    // For double digital gain
    if(DG == VEML6035_DG_1_DOUBLE)
    {
    	if (ALS_IT == VEML6035_ALS_IT_800ms)
		{
			if ((GAIN == VEML6035_GAIN_1_DOUBLE) && (SENS == VEML6035_SENS_0_x1)) Resolution = 0.0004;
			if ((GAIN == VEML6035_GAIN_0_NORMAL) && (SENS == VEML6035_SENS_0_x1)) Resolution = 0.0008;
			if ((GAIN == VEML6035_GAIN_1_DOUBLE) && (SENS == VEML6035_SENS_1_x1_8)) Resolution = 0.0032;
			if ((GAIN == VEML6035_GAIN_0_NORMAL) && (SENS == VEML6035_SENS_1_x1_8)) Resolution = 0.0064;
		}
        if (ALS_IT == VEML6035_ALS_IT_400ms)
        {
        	if ((GAIN == VEML6035_GAIN_1_DOUBLE) && (SENS == VEML6035_SENS_0_x1)) Resolution = 0.0008;
        	if ((GAIN == VEML6035_GAIN_0_NORMAL) && (SENS == VEML6035_SENS_0_x1)) Resolution = 0.0016;
        	if ((GAIN == VEML6035_GAIN_1_DOUBLE) && (SENS == VEML6035_SENS_1_x1_8)) Resolution = 0.0064;
        	if ((GAIN == VEML6035_GAIN_0_NORMAL) && (SENS == VEML6035_SENS_1_x1_8)) Resolution = 0.0128;
        }
        if (ALS_IT == VEML6035_ALS_IT_200ms)
        {
        	if ((GAIN == VEML6035_GAIN_1_DOUBLE) && (SENS == VEML6035_SENS_0_x1)) Resolution = 0.0016;
        	if ((GAIN == VEML6035_GAIN_0_NORMAL) && (SENS == VEML6035_SENS_0_x1)) Resolution = 0.0032;
        	if ((GAIN == VEML6035_GAIN_1_DOUBLE) && (SENS == VEML6035_SENS_1_x1_8)) Resolution = 0.0128;
        	if ((GAIN == VEML6035_GAIN_0_NORMAL) && (SENS == VEML6035_SENS_1_x1_8)) Resolution = 0.0256;
        }
        if (ALS_IT == VEML6035_ALS_IT_100ms)
        {
        	if ((GAIN == VEML6035_GAIN_1_DOUBLE) && (SENS == VEML6035_SENS_0_x1)) Resolution = 0.0032;
        	if ((GAIN == VEML6035_GAIN_0_NORMAL) && (SENS == VEML6035_SENS_0_x1)) Resolution = 0.0064;
        	if ((GAIN == VEML6035_GAIN_1_DOUBLE) && (SENS == VEML6035_SENS_1_x1_8)) Resolution = 0.0256;
        	if ((GAIN == VEML6035_GAIN_0_NORMAL) && (SENS == VEML6035_SENS_1_x1_8)) Resolution = 0.0512;
        }
        if (ALS_IT == VEML6035_ALS_IT_50ms)
        {
        	if ((GAIN == VEML6035_GAIN_1_DOUBLE) && (SENS == VEML6035_SENS_0_x1)) Resolution = 0.0064;
        	if ((GAIN == VEML6035_GAIN_0_NORMAL) && (SENS == VEML6035_SENS_0_x1)) Resolution = 0.0128;
        	if ((GAIN == VEML6035_GAIN_1_DOUBLE) && (SENS == VEML6035_SENS_1_x1_8)) Resolution = 0.0512;
        	if ((GAIN == VEML6035_GAIN_0_NORMAL) && (SENS == VEML6035_SENS_1_x1_8)) Resolution = 0.1024;
        }
        if (ALS_IT == VEML6035_ALS_IT_25ms)
        {
        	if ((GAIN == VEML6035_GAIN_1_DOUBLE) && (SENS == VEML6035_SENS_0_x1)) Resolution = 0.0128;
        	if ((GAIN == VEML6035_GAIN_0_NORMAL) && (SENS == VEML6035_SENS_0_x1)) Resolution = 0.0256;
        	if ((GAIN == VEML6035_GAIN_1_DOUBLE) && (SENS == VEML6035_SENS_1_x1_8)) Resolution = 0.1024;
        	if ((GAIN == VEML6035_GAIN_0_NORMAL) && (SENS == VEML6035_SENS_1_x1_8)) Resolution = 0.2048;
        }
    }
    // For normal digital gain
    if(DG == VEML6035_DG_0_NORMAL)
    {
    	if (ALS_IT == VEML6035_ALS_IT_800ms)
		{
			if ((GAIN == VEML6035_GAIN_1_DOUBLE) && (SENS == VEML6035_SENS_0_x1)) Resolution = 0.0008;
			if ((GAIN == VEML6035_GAIN_0_NORMAL) && (SENS == VEML6035_SENS_0_x1)) Resolution = 0.0016;
			if ((GAIN == VEML6035_GAIN_1_DOUBLE) && (SENS == VEML6035_SENS_1_x1_8)) Resolution = 0.0064;
			if ((GAIN == VEML6035_GAIN_0_NORMAL) && (SENS == VEML6035_SENS_1_x1_8)) Resolution = 0.0128;
		}
		if (ALS_IT == VEML6035_ALS_IT_400ms)
		{
			if ((GAIN == VEML6035_GAIN_1_DOUBLE) && (SENS == VEML6035_SENS_0_x1)) Resolution = 0.0016;
			if ((GAIN == VEML6035_GAIN_0_NORMAL) && (SENS == VEML6035_SENS_0_x1)) Resolution = 0.0032;
			if ((GAIN == VEML6035_GAIN_1_DOUBLE) && (SENS == VEML6035_SENS_1_x1_8)) Resolution = 0.0128;
			if ((GAIN == VEML6035_GAIN_0_NORMAL) && (SENS == VEML6035_SENS_1_x1_8)) Resolution = 0.0256;
		}
		if (ALS_IT == VEML6035_ALS_IT_200ms)
		{
			if ((GAIN == VEML6035_GAIN_1_DOUBLE) && (SENS == VEML6035_SENS_0_x1)) Resolution = 0.0032;
			if ((GAIN == VEML6035_GAIN_0_NORMAL) && (SENS == VEML6035_SENS_0_x1)) Resolution = 0.0064;
			if ((GAIN == VEML6035_GAIN_1_DOUBLE) && (SENS == VEML6035_SENS_1_x1_8)) Resolution = 0.0256;
			if ((GAIN == VEML6035_GAIN_0_NORMAL) && (SENS == VEML6035_SENS_1_x1_8)) Resolution = 0.0512;
		}
		if (ALS_IT == VEML6035_ALS_IT_100ms)
		{
			if ((GAIN == VEML6035_GAIN_1_DOUBLE) && (SENS == VEML6035_SENS_0_x1)) Resolution = 0.0064;
			if ((GAIN == VEML6035_GAIN_0_NORMAL) && (SENS == VEML6035_SENS_0_x1)) Resolution = 0.0128;
			if ((GAIN == VEML6035_GAIN_1_DOUBLE) && (SENS == VEML6035_SENS_1_x1_8)) Resolution = 0.0512;
			if ((GAIN == VEML6035_GAIN_0_NORMAL) && (SENS == VEML6035_SENS_1_x1_8)) Resolution = 0.1024;
		}
		if (ALS_IT == VEML6035_ALS_IT_50ms)
		{
			if ((GAIN == VEML6035_GAIN_1_DOUBLE) && (SENS == VEML6035_SENS_0_x1)) Resolution = 0.0128;
			if ((GAIN == VEML6035_GAIN_0_NORMAL) && (SENS == VEML6035_SENS_0_x1)) Resolution = 0.0256;
			if ((GAIN == VEML6035_GAIN_1_DOUBLE) && (SENS == VEML6035_SENS_1_x1_8)) Resolution = 0.1024;
			if ((GAIN == VEML6035_GAIN_0_NORMAL) && (SENS == VEML6035_SENS_1_x1_8)) Resolution = 0.2048;
		}
		if (ALS_IT == VEML6035_ALS_IT_25ms)
		{
			if ((GAIN == VEML6035_GAIN_1_DOUBLE) && (SENS == VEML6035_SENS_0_x1)) Resolution = 0.0256;
			if ((GAIN == VEML6035_GAIN_0_NORMAL) && (SENS == VEML6035_SENS_0_x1)) Resolution = 0.0512;
			if ((GAIN == VEML6035_GAIN_1_DOUBLE) && (SENS == VEML6035_SENS_1_x1_8)) Resolution = 0.2048;
			if ((GAIN == VEML6035_GAIN_0_NORMAL) && (SENS == VEML6035_SENS_1_x1_8)) Resolution = 0.4096;
		}
    }
    return(Resolution);
}

/*Calculate the ALS Lux
 *VEML6035_CAL_Lux(float Resolution, float Count)
 *float Resolution - Output from VEML6035_CAL_Resolution(Byte DG, Byte ALS_IT, Byte GAIN, Byte SENS)
 *float Count - Output from VEML6035_GET_ALS_DATA()
 *
 *returns lux
 */
float VEML6035_CAL_Lux(float Resolution, float Count)
{
    float Lux;
    Lux = Resolution*Count;
    return(Lux);
}

/*Get ALS_IT
 *VEML6035_GET_ALS_IT()
 *returns ALS_IT
 */
Byte VEML6035_GET_ALS_IT()
{
	if (VEML6035_GET_ALS_IT_Bits() == 1) {return VEML6035_ALS_IT_25ms;}
	if (VEML6035_GET_ALS_IT_Bits() == 2) {return VEML6035_ALS_IT_50ms;}
	if (VEML6035_GET_ALS_IT_Bits() == 3) {return VEML6035_ALS_IT_100ms;}
	if (VEML6035_GET_ALS_IT_Bits() == 4) {return VEML6035_ALS_IT_200ms;}
	if (VEML6035_GET_ALS_IT_Bits() == 5) {return VEML6035_ALS_IT_400ms;}
	if (VEML6035_GET_ALS_IT_Bits() == 6) {return VEML6035_ALS_IT_800ms;}
	else
	return 0;
}

/*Get GAIN
 *VEML6035_GET_GAIN()
 *returns GAIN
 */
Byte VEML6035_GET_GAIN()
{

	if (VEML6035_GET_GAIN_Bit() == 0) {return VEML6035_GAIN_0_NORMAL;}
	if (VEML6035_GET_GAIN_Bit() == 1) {return VEML6035_GAIN_1_DOUBLE;}
	else
	return 0;
}

/*Get DG
 *VEML6035_GET_DG()
 *returns DG
 */
Byte VEML6035_GET_DG()
{

	if (VEML6035_GET_DG_Bit() == 0) {return VEML6035_DG_0_NORMAL;}
	if (VEML6035_GET_DG_Bit() == 1) {return VEML6035_DG_1_DOUBLE;}
	else
	return 0;
}

/*Get SENS
 *VEML6035_GET_SENS()
 *returns SENS
 */
Byte VEML6035_GET_SENS()
{

	if (VEML6035_GET_SENS_Bit() == 0) {return VEML6035_SENS_0_x1;}
	if (VEML6035_GET_SENS_Bit() == 1) {return VEML6035_SENS_1_x1_8;}
	else
	return 0;
}

/*Get Delay for Measurement
 *VEML6035_GET_Delay()
 *returns delay in ms
 */
int VEML6035_GET_Delay()
{
	int Delay;
	int i = 0;

	//Delay for Normal ALS Mode (Power Saving Mode Disable)
	if (VEML6035_GET_PSM_EN_Bit() == 0b0)
	{
		//Delay for ALS
		//Delay = Delay from IT + Circuit (~10ms)
		if (VEML6035_GET_ALS_IT_Bits() == 1) {Delay = 35;}
		if (VEML6035_GET_ALS_IT_Bits() == 2) {Delay = 60;}
		if (VEML6035_GET_ALS_IT_Bits() == 3) {Delay = 110;}
		if (VEML6035_GET_ALS_IT_Bits() == 4) {Delay = 210;}
		if (VEML6035_GET_ALS_IT_Bits() == 5) {Delay = 410;}
		if (VEML6035_GET_ALS_IT_Bits() == 6) {Delay = 810;}
	}

	//Delay for Power Saving Mode
	if (VEML6035_GET_PSM_EN_Bit() == 0b1)
	{
		//Check PSM Waiting Time
		if (VEML6035_GET_PSM_WAIT_Bits() == 1){i = 400;}
		if (VEML6035_GET_PSM_WAIT_Bits() == 2){i = 800;}
		if (VEML6035_GET_PSM_WAIT_Bits() == 3){i = 1600;}
		if (VEML6035_GET_PSM_WAIT_Bits() == 4){i = 3200;}

		//Delay = Delay from IT + Circuit (~10ms)
		if (VEML6035_GET_ALS_IT_Bits() == 1) {Delay = i + 35;}
		if (VEML6035_GET_ALS_IT_Bits() == 2) {Delay = i + 60;}
		if (VEML6035_GET_ALS_IT_Bits() == 3) {Delay = i + 110;}
		if (VEML6035_GET_ALS_IT_Bits() == 4) {Delay = i + 210;}
		if (VEML6035_GET_ALS_IT_Bits() == 5) {Delay = i + 410;}
		if (VEML6035_GET_ALS_IT_Bits() == 6) {Delay = i + 810;}
	}

	return Delay;
}

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
int VEML6035_GET_ALS_Mode()
{
	int Mode;

	//Read the ALS_SD bit: Mode = 0 - ALS Shutdown
	if (VEML6035_GET_SD_Bit() == 0b1) Mode = 0;

	//Read the ALS_SD bit: Mode = 1 - Auto Mode (Only ALS)
	if ((VEML6035_GET_SD_Bit() == 0b0) && (VEML6035_GET_CHANNEL_EN_Bit() == 0b0) && (VEML6035_GET_PSM_EN_Bit() == 0b0)) Mode = 1;

	//Read the PSM bits: Mode = 2 - PSM with waiting time 0.4s (Only ALS)
	if ((VEML6035_GET_SD_Bit() == 0b0) && (VEML6035_GET_PSM_EN_Bit() == 0b1) && (VEML6035_GET_PSM_WAIT_Bits() == 1) && (VEML6035_GET_CHANNEL_EN_Bit() == 0b0)) Mode = 2;

	//Read the PSM bits: Mode = 3 - PSM with waiting time 0.8s (Only ALS)
	if ((VEML6035_GET_SD_Bit() == 0b0) && (VEML6035_GET_PSM_EN_Bit() == 0b1) && (VEML6035_GET_PSM_WAIT_Bits() == 2) && (VEML6035_GET_CHANNEL_EN_Bit() == 0b0)) Mode = 3;

	//Read the PSM bits: Mode = 4 - PSM with waiting time 1.6s (Only ALS)
	if ((VEML6035_GET_SD_Bit() == 0b0) && (VEML6035_GET_PSM_EN_Bit() == 0b1) && (VEML6035_GET_PSM_WAIT_Bits() == 3) && (VEML6035_GET_CHANNEL_EN_Bit() == 0b0)) Mode = 4;

	//Read the PSM bits: Mode = 5 - PSM with waiting time 3.2s (Only ALS)
	if ((VEML6035_GET_SD_Bit() == 0b0) && (VEML6035_GET_PSM_EN_Bit() == 0b1) && (VEML6035_GET_PSM_WAIT_Bits() == 4) && (VEML6035_GET_CHANNEL_EN_Bit() == 0b0)) Mode = 5;

	//Read the ALS_SD bit: Mode = 6 - Auto Mode (ALS + White)
	if ((VEML6035_GET_SD_Bit() == 0b0) && (VEML6035_GET_CHANNEL_EN_Bit() == 0b1) && (VEML6035_GET_PSM_EN_Bit() == 0b0)) Mode = 6;

	//Read the PSM bits: Mode = 7 - PSM with waiting time 0.4s (ALS + White)
	if ((VEML6035_GET_SD_Bit() == 0b0) && (VEML6035_GET_PSM_EN_Bit() == 0b1) && (VEML6035_GET_PSM_WAIT_Bits() == 1) && (VEML6035_GET_CHANNEL_EN_Bit() == 0b1)) Mode = 7;

	//Read the PSM bits: Mode = 8 - PSM with waiting time 0.8s (ALS + White)
	if ((VEML6035_GET_SD_Bit() == 0b0) && (VEML6035_GET_PSM_EN_Bit() == 0b1) && (VEML6035_GET_PSM_WAIT_Bits() == 2) && (VEML6035_GET_CHANNEL_EN_Bit() == 0b1)) Mode = 8;

	//Read the PSM bits: Mode = 9 - PSM with waiting time 1.6s (ALS + White)
	if ((VEML6035_GET_SD_Bit() == 0b0) && (VEML6035_GET_PSM_EN_Bit() == 0b1) && (VEML6035_GET_PSM_WAIT_Bits() == 3) && (VEML6035_GET_CHANNEL_EN_Bit() == 0b1)) Mode = 9;

	//Read the PSM bits: Mode = 10 - PSM with waiting time 3.2s (ALS + White)
	if ((VEML6035_GET_SD_Bit() == 0b0) && (VEML6035_GET_PSM_EN_Bit() == 0b1) && (VEML6035_GET_PSM_WAIT_Bits() == 4) && (VEML6035_GET_CHANNEL_EN_Bit() == 0b1)) Mode = 10;

	return Mode;
}


//Reset the Sensor to default Value
void Reset_Sensor()
{
	struct TransferData VEML6035_Data;
	VEML6035_Data.Slave_Address = VEML6035_Slave_Address;
	VEML6035_Data.RegisterAddress = VEML6035_ALS_CONF_0;
	VEML6035_Data.Select_I2C_Bus = I2C_Bus;
	VEML6035_Data.WData[0] = 0x01;
	VEML6035_Data.WData[1] = 0x00;
	WriteI2C_Bus(&VEML6035_Data);

	VEML6035_Data.Slave_Address = VEML6035_Slave_Address;
	VEML6035_Data.RegisterAddress = VEML6035_WH;
	VEML6035_Data.Select_I2C_Bus = I2C_Bus;
	VEML6035_Data.WData[0] = 0x00;
	VEML6035_Data.WData[1] = 0x00;
	WriteI2C_Bus(&VEML6035_Data);

	VEML6035_Data.Slave_Address = VEML6035_Slave_Address;
	VEML6035_Data.RegisterAddress = VEML6035_WL;
	VEML6035_Data.Select_I2C_Bus = I2C_Bus;
	VEML6035_Data.WData[0] = 0x00;
	VEML6035_Data.WData[1] = 0x00;
	WriteI2C_Bus(&VEML6035_Data);

	VEML6035_Data.Slave_Address = VEML6035_Slave_Address;
	VEML6035_Data.RegisterAddress = VEML6035_PSM;
	VEML6035_Data.Select_I2C_Bus = I2C_Bus;
	VEML6035_Data.WData[0] = 0x00;
	VEML6035_Data.WData[1] = 0x00;
	WriteI2C_Bus(&VEML6035_Data);
}



//Print the output of the sensor
void ALS_White_Reading()
{
	Word ALS_value, White_value;
	// int Delay;
	float Resolution;
	// float Lux;
	Byte ALS_IT = VEML6035_GET_ALS_IT();
	Byte GAIN = VEML6035_GET_GAIN();
	Byte DG = VEML6035_GET_DG();
	Byte SENS = VEML6035_GET_SENS();

	// info_displayln("***************************************************");
    // delay(5);

    // info_displayln(">>>>>>>>>>>>>>>>>>>>>>>ALS<<<<<<<<<<<<<<<<<<<<<<<<<");
	// delay(5);

    //Print ALS Data
	//Get Delay for ALS Measurement
	// Delay = VEML6035_GET_Delay();

	//Delay of IT ms + other Circuit Delay (~10ms)
	// delay(Delay);

	//Find Resolution
	Resolution = VEML6035_CAL_Resolution(DG, ALS_IT, GAIN, SENS);

	//Print the Resolution
	// info_display(">>>>>>>Resolution : ");
	// info_display(Resolution,5);
	// info_displayln(" lx/Count<<<<<<<<");
	// delay(50);

	//Print the ALS Data in DEC
	ALS_value = VEML6035_GET_ALS_DATA();
	// info_display(">>>>>>>ALS Data : ");
	// info_display(value,DEC);
	// info_displayln(" Counts<<<<<<<<");
	// delay(50);

	//Calculate Lux
	// Lux = VEML6035_CAL_Lux(Resolution, value);

	//Print the ALS Lux in DEC
	// info_display(">>>>>>>ALS Lux : ");
	// info_display(Lux,5);
	// info_displayln(" lx<<<<<<<<");
	// delay(50);

	//Print the White Data in DEC
	White_value = VEML6035_GET_WHITE_DATA();		
	// info_display(">>>>>>>White Data : ");
	// info_display(value,DEC);
	// info_displayln(" Counts<<<<<<<<");
	// delay(50);

	info_displayf("%d,%d\n", ALS_value, White_value);

	// info_displayln("***************************************************");
	// delay(50);

	//Print the Interrupt Flag
	// value = VEML6035_GET_IF();
	// info_display(">>>>>>>Interrupt Flag : 0b");
	// info_display(value,BIN);
	// info_displayln("<<<<<<<<");
	// delay(50);

	// info_displayln("***************************************************");
	// delay(50);

	// info_displayln("");
	// delay(50);

	// info_displayln("");

	// delay(2000);
}



//Print the output of the sensor
void Print_Data_Only()
{
	Word value;
	int Delay;
	float Resolution;
	float Lux;
	Byte ALS_IT = VEML6035_GET_ALS_IT();
	Byte GAIN = VEML6035_GET_GAIN();
	Byte DG = VEML6035_GET_DG();
	Byte SENS = VEML6035_GET_SENS();

	info_displayln("***************************************************");
    delay(5);

    info_displayln(">>>>>>>>>>>>>>>>>>>>>>>ALS<<<<<<<<<<<<<<<<<<<<<<<<<");
	delay(5);

    //Print ALS Data
	//Get Delay for ALS Measurement
	Delay = VEML6035_GET_Delay();

	//Delay of IT ms + other Circuit Delay (~10ms)
	delay(Delay);

	//Find Resolution
	Resolution = VEML6035_CAL_Resolution(DG, ALS_IT, GAIN, SENS);

	//Print the Resolution
	info_display(">>>>>>>Resolution : ");
	info_display(Resolution,5);
	info_displayln(" lx/Count<<<<<<<<");
	delay(50);

	//Print the ALS Data in DEC
	value = VEML6035_GET_ALS_DATA();
	info_display(">>>>>>>ALS Data : ");
	info_display(value,DEC);
	info_displayln(" Counts<<<<<<<<");
	delay(50);

	//Calculate Lux
	Lux = VEML6035_CAL_Lux(Resolution, value);

	//Print the ALS Lux in DEC
	info_display(">>>>>>>ALS Lux : ");
	info_display(Lux,5);
	info_displayln(" lx<<<<<<<<");
	delay(50);

	//Print the White Data in DEC
	value = VEML6035_GET_WHITE_DATA();		
	info_display(">>>>>>>White Data : ");
	info_display(value,DEC);
	info_displayln(" Counts<<<<<<<<");
	delay(50);

	info_displayln("***************************************************");
	delay(50);

	//Print the Interrupt Flag
	value = VEML6035_GET_IF();
	info_display(">>>>>>>Interrupt Flag : 0b");
	info_display(value,BIN);
	info_displayln("<<<<<<<<");
	delay(50);

	info_displayln("***************************************************");
	delay(50);

	info_displayln("");
	delay(50);

	info_displayln("");

	delay(2000);
}

/*Print the variable in DEC for debugging
 *Print_Variable_DEC(Word Var)
 *Word Var - Input Parameter:
 *
 * Value between 0d0 and 0d65535
 */
void Print_Variable_DEC(Word Var)
{
	info_displayln("***************************************************");
	delay(50);

	info_display(">>>>>>>Variable : 0d");
	info_displayln(Var,DEC);
	delay(50);

	info_displayln("***************************************************");

	delay(2000);
}

/*Print the variable in HEX for debugging
 *Print_Variable_HEX(Word Var)
 *Word Var - Input Parameter:
 *
 * Value between 0d0 and 0d65535
 */
void Print_Variable_HEX(Word Var)
{
	info_displayln("***************************************************");
	delay(50);

	info_display(">>>>>>>Variable : 0x");
	info_displayln(Var,HEX);
	delay(50);

	info_displayln("***************************************************");

	delay(2000);
}

/*Reverses a string 'str' of length 'len'
 *reverse(char* str, int len)
 *char* str - Array pointer to be reversed
 *int len - Length of the array
 */
void reverse(char* str, int len)
{
    int i = 0, j = len - 1, temp;
    while (i < j) {
        temp = str[i];
        str[i] = str[j];
        str[j] = temp;
        i++;
        j--;
    }
}

/*Converts a given integer x to string str[].
 *intToStr(int x, char str[], int d)
 *int x - floating-point number to be converted to a string (Both the integer part as well as fraction/decimal point part)
 *char str[] - output string of the floating-point number in the form of array character
 *int d - number of decimal point (the integer part always = 0, the fraction/decimal point part = int afterpoint from ftoa() input)
 */
int intToStr(int x, char str[], int d)
{
    int i = 0;

    while (x)
	{
		//Store and convert int to char (Valid for single digit)
        str[i++] = (x % 10) + '0';
        x = x / 10;
    }

    //If number of digits required is more, then
    //add 0s at the beginning
    while (i < d) str[i++] = '0';

	//Reverse the string characters in the array str
    reverse(str, i);

	//Place the null character at the end of the array
    str[i] = '\0';

	//Return the position i
    return i;
}

/*Converts a floating-point/double number to string.
 *ftoa(float n, char* res, int afterpoint)
 *float n - floating-point number to be converted to a string
 *char* res - pointer to output string of the floating-point number in the form of array character
 *int d - number of decimal point
 */
void ftoa(float n, char* res, int afterpoint)
{
    //Extract integer part
    int ipart = (int)n;

    //Extract decimal part
    float fpart = n - (float)ipart;

    //Convert integer part to string and the function returns the position after the interger
    int i = intToStr(ipart, res, 0);

    //Check for display option after point
    if (afterpoint != 0)
	{
		//Add dot after the integer part
        res[i] = '.';

        //Multiply decimal part by 10^decimal point
        fpart = fpart* pow(10, afterpoint);

		//Convert decimal part to string
        intToStr((int)fpart, res + i + 1, afterpoint);
    }
}
