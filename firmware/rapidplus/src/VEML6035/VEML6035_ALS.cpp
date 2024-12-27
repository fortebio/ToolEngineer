/*
 * VEML6035_ALS.cpp
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
//*****************************************Sensor API*************************************************

/*Set the Sensitivity
 *VEML6035_SET_SENS(Byte Sens)
 *Byte Sens - Input Parameter:
 *
 * VEML6035_SENS_0_x1
 * VEML6035_SENS_1_x1_8
 */
void VEML6035_SET_SENS(Byte Sens)
{
	struct TransferData VEML6035_Data;
	VEML6035_Data.Slave_Address = VEML6035_Slave_Address;
	VEML6035_Data.RegisterAddress = VEML6035_ALS_CONF_0;
	VEML6035_Data.Select_I2C_Bus = I2C_Bus;
	ReadI2C_Bus(&VEML6035_Data);
	VEML6035_Data.WData[0] = VEML6035_Data.RData[0];
	VEML6035_Data.WData[1] = (VEML6035_Data.RData[1]&~(VEML6035_SENS_0_x1|VEML6035_SENS_1_x1_8))|Sens;
	WriteI2C_Bus(&VEML6035_Data);
}

/*Set the Digital Gain (DG)
 *VEML6035_SET_DG(Byte DG)
 *Byte DG - Input Parameter:
 *
 * VEML6035_DG_0_NORMAL
 * VEML6035_DG_1_DOUBLE
 */
void VEML6035_SET_DG(Byte DG)
{
	struct TransferData VEML6035_Data;
	VEML6035_Data.Slave_Address = VEML6035_Slave_Address;
	VEML6035_Data.RegisterAddress = VEML6035_ALS_CONF_0;
	VEML6035_Data.Select_I2C_Bus = I2C_Bus;
	ReadI2C_Bus(&VEML6035_Data);
	VEML6035_Data.WData[0] = VEML6035_Data.RData[0];
	VEML6035_Data.WData[1] = (VEML6035_Data.RData[1]&~(VEML6035_DG_0_NORMAL|VEML6035_DG_1_DOUBLE))|DG;
	WriteI2C_Bus(&VEML6035_Data);
}

/*Set the Gain
 *VEML6035_SET_GAIN(Byte Gain)
 *Byte Gain - Input Parameter:
 *
 * VEML6035_GAIN_0_NORMAL
 * VEML6035_GAIN_1_DOUBLE
 */
void VEML6035_SET_GAIN(Byte Gain)
{
	struct TransferData VEML6035_Data;
	VEML6035_Data.Slave_Address = VEML6035_Slave_Address;
	VEML6035_Data.RegisterAddress = VEML6035_ALS_CONF_0;
	VEML6035_Data.Select_I2C_Bus = I2C_Bus;
	ReadI2C_Bus(&VEML6035_Data);
	VEML6035_Data.WData[0] = VEML6035_Data.RData[0];
	VEML6035_Data.WData[1] = (VEML6035_Data.RData[1]&~(VEML6035_GAIN_0_NORMAL|VEML6035_GAIN_1_DOUBLE))|Gain;
	WriteI2C_Bus(&VEML6035_Data);
}

/*Set the Integration Time
 *VEML6035_SET_ALS_IT(Byte IntTime)
 *Byte IntTime - Input Parameter:
 *
 * VEML6035_ALS_IT_25ms
 * VEML6035_ALS_IT_50ms
 * VEML6035_ALS_IT_100ms
 * VEML6035_ALS_IT_200ms
 * VEML6035_ALS_IT_400ms
 * VEML6035_ALS_IT_800ms
 */
void VEML6035_SET_ALS_IT(Byte IntTime)
{
	struct TransferData VEML6035_Data;
	VEML6035_Data.Slave_Address = VEML6035_Slave_Address;
	VEML6035_Data.RegisterAddress = VEML6035_ALS_CONF_0;
	VEML6035_Data.Select_I2C_Bus = I2C_Bus;
	ReadI2C_Bus(&VEML6035_Data);

    if(IntTime == VEML6035_ALS_IT_100ms||IntTime == VEML6035_ALS_IT_200ms||IntTime == VEML6035_ALS_IT_400ms||IntTime == VEML6035_ALS_IT_800ms)
    {
	    VEML6035_Data.WData[0] = (VEML6035_Data.RData[0]&~(VEML6035_ALS_IT_100ms|VEML6035_ALS_IT_200ms|VEML6035_ALS_IT_400ms|VEML6035_ALS_IT_800ms))|IntTime;
	    VEML6035_Data.WData[1] = VEML6035_Data.RData[1];
    }

    if(IntTime == VEML6035_ALS_IT_25ms||IntTime == VEML6035_ALS_IT_50ms)
    {
	    VEML6035_Data.WData[0] = VEML6035_Data.RData[0];
	    VEML6035_Data.WData[1] = (VEML6035_Data.RData[1]&~(VEML6035_ALS_IT_25ms|VEML6035_ALS_IT_50ms))|IntTime;
    }
    WriteI2C_Bus(&VEML6035_Data);
}

/*Set the Persistence
 *VEML6035_SET_ALS_PERS(Byte Pers)
 *Byte Pers - Input Parameter:
 *
 * VEML6035_ALS_PERS_1
 * VEML6035_ALS_PERS_2
 * VEML6035_ALS_PERS_4
 * VEML6035_ALS_PERS_8
 */
void VEML6035_SET_ALS_PERS(Byte Pers)
{
	struct TransferData VEML6035_Data;
	VEML6035_Data.Slave_Address = VEML6035_Slave_Address;
	VEML6035_Data.RegisterAddress = VEML6035_ALS_CONF_0;
	VEML6035_Data.Select_I2C_Bus = I2C_Bus;
	ReadI2C_Bus(&VEML6035_Data);
	VEML6035_Data.WData[0] = (VEML6035_Data.RData[0]&~(VEML6035_ALS_PERS_1|VEML6035_ALS_PERS_2|VEML6035_ALS_PERS_4|VEML6035_ALS_PERS_8))|Pers;
	VEML6035_Data.WData[1] = VEML6035_Data.RData[1];
	WriteI2C_Bus(&VEML6035_Data);
}

/*Set the Interrupt Channel
 *VEML6035_SET_INT_CHANNEL(Byte Int_Channel)
 *Byte Int_Channel - Input Parameter:
 *
 * VEML6035_ALS_CH_INT_EN
 * VEML6035_WHITE_CH_INT_EN
 */
void VEML6035_SET_INT_CHANNEL(Byte Int_Channel)
{
	struct TransferData VEML6035_Data;
	VEML6035_Data.Slave_Address = VEML6035_Slave_Address;
	VEML6035_Data.RegisterAddress = VEML6035_ALS_CONF_0;
	VEML6035_Data.Select_I2C_Bus = I2C_Bus;
	ReadI2C_Bus(&VEML6035_Data);
	VEML6035_Data.WData[0] = (VEML6035_Data.RData[0]&~(VEML6035_ALS_CH_INT_EN|VEML6035_WHITE_CH_INT_EN))|Int_Channel;
	VEML6035_Data.WData[1] = VEML6035_Data.RData[1];
	WriteI2C_Bus(&VEML6035_Data);
}

/*Enable/Disable White Channel
 *VEML6035_SET_CHANNEL_EN(Byte White)
 *Byte White - Input Parameter:
 *
 * VEML6035_WHITE_CH_DIS
 * VEML6035_WHITE_CH_EN
 */
void VEML6035_SET_CHANNEL_EN(Byte White)
{
	struct TransferData VEML6035_Data;
	VEML6035_Data.Slave_Address = VEML6035_Slave_Address;
	VEML6035_Data.RegisterAddress = VEML6035_ALS_CONF_0;
	VEML6035_Data.Select_I2C_Bus = I2C_Bus;
	ReadI2C_Bus(&VEML6035_Data);
	VEML6035_Data.WData[0] = (VEML6035_Data.RData[0]&~(VEML6035_WHITE_CH_DIS|VEML6035_WHITE_CH_EN))|White;
	VEML6035_Data.WData[1] = VEML6035_Data.RData[1];
	WriteI2C_Bus(&VEML6035_Data);
}

/*Enable/Disable Interrupt
 *VEML6035_SET_INT_EN(Byte Interrupt)
 *Byte Interrupt - Input Parameter:
 *
 * VEML6035_ALS_INT_EN
 * VEML6035_ALS_INT_DIS
 */
void VEML6035_SET_INT_EN(Byte Interrupt)
{
	struct TransferData VEML6035_Data;
	VEML6035_Data.Slave_Address = VEML6035_Slave_Address;
	VEML6035_Data.RegisterAddress = VEML6035_ALS_CONF_0;
	VEML6035_Data.Select_I2C_Bus = I2C_Bus;
	ReadI2C_Bus(&VEML6035_Data);
	VEML6035_Data.WData[0] = (VEML6035_Data.RData[0]&~(VEML6035_ALS_INT_EN|VEML6035_ALS_INT_DIS))|Interrupt;
	VEML6035_Data.WData[1] = VEML6035_Data.RData[1];
	WriteI2C_Bus(&VEML6035_Data);
}

/*Turn On/Off the ALS Sensor
 *VEML6035_SET_SD(Byte SD_Bit)
 *Byte SD_Bit - Input Parameter:
 *
 * VEML6035_ALS_SD_ON
 * VEML6035_ALS_SD_OFF
 *
 */
void VEML6035_SET_SD(Byte SD_Bit)
{
	struct TransferData VEML6035_Data;
	VEML6035_Data.Slave_Address = VEML6035_Slave_Address;
	VEML6035_Data.RegisterAddress = VEML6035_ALS_CONF_0;
	VEML6035_Data.Select_I2C_Bus = I2C_Bus;
	ReadI2C_Bus(&VEML6035_Data);
	VEML6035_Data.WData[0] = (VEML6035_Data.RData[0]&~(VEML6035_ALS_SD_ON|VEML6035_ALS_SD_OFF))|SD_Bit;
	VEML6035_Data.WData[1] = VEML6035_Data.RData[1];
	WriteI2C_Bus(&VEML6035_Data);
}

/*Set the High Threshold
 *VEML6035_SET_ALS_HighThreshold(Word HighThreshold);
 *Word HighThreshold - Input Parameter:
 *
 * Value between 0d0 and 0d65535
 */
void VEML6035_SET_ALS_HighThreshold(Word HighThreshold)
{
 	struct TransferData VEML6035_Data;
	VEML6035_Data.Slave_Address = VEML6035_Slave_Address;
	VEML6035_Data.RegisterAddress = VEML6035_WH;
	VEML6035_Data.Select_I2C_Bus = I2C_Bus;
	VEML6035_Data.WData[0] = HighThreshold;
	VEML6035_Data.WData[1] = (HighThreshold>>8);
	WriteI2C_Bus(&VEML6035_Data);
}

/*Set the Low Threshold
 *VEML6035_SET_ALS_LowThreshold(Word LowThreshold)
 *Word LowThreshold - Input Parameter:
 *
 * Value between 0d0 and 0d65535
 */
void VEML6035_SET_ALS_LowThreshold(Word LowThreshold)
{
	struct TransferData VEML6035_Data;
	VEML6035_Data.Slave_Address = VEML6035_Slave_Address;
	VEML6035_Data.RegisterAddress = VEML6035_WL;
	VEML6035_Data.Select_I2C_Bus = I2C_Bus;
	VEML6035_Data.WData[0] = LowThreshold;
	VEML6035_Data.WData[1] = (LowThreshold>>8);
	WriteI2C_Bus(&VEML6035_Data);
}

/*Set Power Saving Mode Waiting Time
 *VEML6035_SET_PSM_WAIT(Byte PSM_Wait)
 *Byte PSM_Wait - Input Parameter:
 *
 * VEML6035_ALS_PSM_WAIT_0_4
 * VEML6035_ALS_PSM_WAIT_0_8
 * VEML6035_ALS_PSM_WAIT_1_6
 * VEML6035_ALS_PSM_WAIT_3_2
 */
void VEML6035_SET_PSM_WAIT(Byte PSM_Wait)
{
	struct TransferData VEML6035_Data;
	VEML6035_Data.Slave_Address = VEML6035_Slave_Address;
	VEML6035_Data.RegisterAddress = VEML6035_PSM;
	VEML6035_Data.Select_I2C_Bus = I2C_Bus;
	ReadI2C_Bus(&VEML6035_Data);
	VEML6035_Data.WData[0] = (VEML6035_Data.RData[0]&~(VEML6035_ALS_PSM_WAIT_0_4|VEML6035_ALS_PSM_WAIT_0_8|VEML6035_ALS_PSM_WAIT_1_6|VEML6035_ALS_PSM_WAIT_3_2))|PSM_Wait;
	VEML6035_Data.WData[1] = VEML6035_Data.RData[1];
	WriteI2C_Bus(&VEML6035_Data);
}


/*Enable/Disable the Power Saving Mode
 *VEML6035_SET_PSM_EN(Byte PSM_En)
 *Byte PSM_En - Input Parameter:
 *
 * VEML6035_ALS_PSM_EN
 * VEML6035_ALS_PSM_DIS
 */
void VEML6035_SET_PSM_EN(Byte PSM_En)
{
	struct TransferData VEML6035_Data;
	VEML6035_Data.Slave_Address = VEML6035_Slave_Address;
	VEML6035_Data.RegisterAddress = VEML6035_PSM;
	VEML6035_Data.Select_I2C_Bus = I2C_Bus;
	ReadI2C_Bus(&VEML6035_Data);
	VEML6035_Data.WData[0] = (VEML6035_Data.RData[0]&~(VEML6035_ALS_PSM_EN|VEML6035_ALS_PSM_DIS))|PSM_En;
	VEML6035_Data.WData[1] = VEML6035_Data.RData[1];
	WriteI2C_Bus(&VEML6035_Data);
}

/*Read the ALS Data
 *VEML6035_GET_ALS_DATA() returns ALS Data between 0d0 and 0d65535
 */
Word VEML6035_GET_ALS_DATA()
{
	Word RData=0;
	struct TransferData VEML6035_Data;
	VEML6035_Data.Slave_Address = VEML6035_Slave_Address;
	VEML6035_Data.RegisterAddress = VEML6035_ALS;
	VEML6035_Data.Select_I2C_Bus = I2C_Bus;
	ReadI2C_Bus(&VEML6035_Data);
	RData = ((VEML6035_Data.RData[1]<<8)|VEML6035_Data.RData[0]);
	return RData;
}

/*Read the ALS Data with err status return
 *VEML6035_GET_ALS_DATA() returns I2C reading status, with ALS Data between 0d0 and 0d65535 transferred by function parameter
 */
bool VEML6035_GET_ALS_DATA_I2C_Res(Word * RData)
{
	// Word RData=0;
	struct TransferData VEML6035_Data;
	VEML6035_Data.Slave_Address = VEML6035_Slave_Address;
	VEML6035_Data.RegisterAddress = VEML6035_ALS;
	VEML6035_Data.Select_I2C_Bus = I2C_Bus;
	bool flagI2C = ReadI2C_Bus(&VEML6035_Data);
	*RData = ((VEML6035_Data.RData[1]<<8)|VEML6035_Data.RData[0]);
	return flagI2C;
}

/*Read the White Channel Data
 *VEML6035_GET_WHITE_DATA() returns White Channel Data between 0d0 and 0d65535
 */
Word VEML6035_GET_WHITE_DATA()
{
	Word RData=0;
	struct TransferData VEML6035_Data;
	VEML6035_Data.Slave_Address = VEML6035_Slave_Address;
	VEML6035_Data.RegisterAddress = VEML6035_WHITE;
	VEML6035_Data.Select_I2C_Bus = I2C_Bus;
	ReadI2C_Bus(&VEML6035_Data);
	RData = ((VEML6035_Data.RData[1]<<8)|VEML6035_Data.RData[0]);
	return RData;
}

/*Read the ALS Interrupt Flag
 *VEML6035_GET_IF() returns interrupt flag status.
 *Please refer to Table 7 in the Datasheet page 8
 */
Byte VEML6035_GET_IF()
{
	struct TransferData VEML6035_Data;
	VEML6035_Data.Slave_Address = VEML6035_Slave_Address;
	VEML6035_Data.RegisterAddress = VEML6035_IF;
	VEML6035_Data.Select_I2C_Bus = I2C_Bus;
	ReadI2C_Bus(&VEML6035_Data);
	return (VEML6035_Data.RData[1]&0xC0);
}

/*Read Register value
 *VEML6035_READ_Reg(Byte Reg)
 *Byte Reg - Input Parameter:
 *
 * VEML6035_ALS_CONF_0
 * VEML6035_WH
 * VEML6035_WL
 * VEML6035_PSM
 * VEML6035_ALS
 * VEML6035_WHITE
 * VEML6035_IF
 *
 *returns Register Value between 0d0/0x00 and 0d65535/0xFFFF
 */
Word VEML6035_READ_Reg(Byte Reg)
{
	struct TransferData VEML6035_Data;
	VEML6035_Data.Slave_Address = VEML6035_Slave_Address;
	VEML6035_Data.RegisterAddress = Reg;
	VEML6035_Data.Select_I2C_Bus = I2C_Bus;
	ReadI2C_Bus(&VEML6035_Data);
	return (VEML6035_Data.RData[1]<<8|VEML6035_Data.RData[0]);
}

/*Read the SD bit
 *returns 0 for ALS channel on and 1 for shutdown
 */
bool VEML6035_GET_SD_Bit()
{
	struct TransferData VEML6035_Data;
	VEML6035_Data.Slave_Address = VEML6035_Slave_Address;
	VEML6035_Data.RegisterAddress = VEML6035_ALS_CONF_0;
	VEML6035_Data.Select_I2C_Bus = I2C_Bus;
	ReadI2C_Bus(&VEML6035_Data);
	return (VEML6035_Data.RData[0]&0x01);
}

/*Read the ALS_IT bits
 *returns 0d0 - 0d6 depending on ALS_IT bits
 */
int VEML6035_GET_ALS_IT_Bits()
{
	struct TransferData VEML6035_Data;
	VEML6035_Data.Slave_Address = VEML6035_Slave_Address;
	VEML6035_Data.RegisterAddress = VEML6035_ALS_CONF_0;
	VEML6035_Data.Select_I2C_Bus = I2C_Bus;
	ReadI2C_Bus(&VEML6035_Data);
	if ((VEML6035_Data.RData[1]&0x03) == 0x03) {return 1;}
	if ((VEML6035_Data.RData[1]&0x03) == 0x02) {return 2;}
	if ((VEML6035_Data.RData[0]&0xC0) == 0x00) {return 3;}
	if ((VEML6035_Data.RData[0]&0xC0) == 0x40) {return 4;}
	if ((VEML6035_Data.RData[0]&0xC0) == 0x80) {return 5;}
	if ((VEML6035_Data.RData[0]&0xC0) == 0xC0) {return 6;}
	else
	return 0;
}

/*Read the GAIN bit
 *returns 0 for normal sensitivity and 1 for double sensitivity
 */
bool VEML6035_GET_GAIN_Bit()
{
	struct TransferData VEML6035_Data;
	VEML6035_Data.Slave_Address = VEML6035_Slave_Address;
	VEML6035_Data.RegisterAddress = VEML6035_ALS_CONF_0;
	VEML6035_Data.Select_I2C_Bus = I2C_Bus;
	ReadI2C_Bus(&VEML6035_Data);
	return (VEML6035_Data.RData[1]&0x04);
}

/*Read the DG bit
 *returns 0 for normal and 1 for double
 */
bool VEML6035_GET_DG_Bit()
{
	struct TransferData VEML6035_Data;
	VEML6035_Data.Slave_Address = VEML6035_Slave_Address;
	VEML6035_Data.RegisterAddress = VEML6035_ALS_CONF_0;
	VEML6035_Data.Select_I2C_Bus = I2C_Bus;
	ReadI2C_Bus(&VEML6035_Data);
	return (VEML6035_Data.RData[1]&0x08);
}

/*Read the SENS bit
 *returns 0 for high sensitivity (1x) and 1 for low sensitivity (x1/8)
 */
bool VEML6035_GET_SENS_Bit()
{
	struct TransferData VEML6035_Data;
	VEML6035_Data.Slave_Address = VEML6035_Slave_Address;
	VEML6035_Data.RegisterAddress = VEML6035_ALS_CONF_0;
	VEML6035_Data.Select_I2C_Bus = I2C_Bus;
	ReadI2C_Bus(&VEML6035_Data);
	return (VEML6035_Data.RData[1]&0x10);
}

/*Read the PSM_EN bit
 *returns 0 for Power Saving Mode Disable and 1 for Power Saving Mode Enable
 */
bool VEML6035_GET_PSM_EN_Bit()
{
	struct TransferData VEML6035_Data;
	VEML6035_Data.Slave_Address = VEML6035_Slave_Address;
	VEML6035_Data.RegisterAddress = VEML6035_PSM;
	VEML6035_Data.Select_I2C_Bus = I2C_Bus;
	ReadI2C_Bus(&VEML6035_Data);
	return (VEML6035_Data.RData[0]&0x01);
}

/*Read the PSM_WAIT bit
 *returns 0d0 - 0d4 depending on PSM_WAIT bits
 */
int VEML6035_GET_PSM_WAIT_Bits()
{
	struct TransferData VEML6035_Data;
	VEML6035_Data.Slave_Address = VEML6035_Slave_Address;
	VEML6035_Data.RegisterAddress = VEML6035_PSM;
	VEML6035_Data.Select_I2C_Bus = I2C_Bus;
	ReadI2C_Bus(&VEML6035_Data);
	if ((VEML6035_Data.RData[0]&0x06) == 0x00) {return 1;}
	if ((VEML6035_Data.RData[0]&0x06) == 0x02) {return 2;}
	if ((VEML6035_Data.RData[0]&0x06) == 0x04) {return 3;}
	if ((VEML6035_Data.RData[0]&0x06) == 0x06) {return 4;}
	else
	return 0;
}

/*Read the CHANNEL_EN bit
 *returns 0 for ALS channel enable (Disable White channel) and 1 for ALS + White channel enable
 */
bool VEML6035_GET_CHANNEL_EN_Bit()
{
	struct TransferData VEML6035_Data;
	VEML6035_Data.Slave_Address = VEML6035_Slave_Address;
	VEML6035_Data.RegisterAddress = VEML6035_ALS_CONF_0;
	VEML6035_Data.Select_I2C_Bus = I2C_Bus;
	ReadI2C_Bus(&VEML6035_Data);
	return (VEML6035_Data.RData[0]&0x04);
}
