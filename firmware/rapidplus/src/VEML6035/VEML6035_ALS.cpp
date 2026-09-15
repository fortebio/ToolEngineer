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
/***********************************************************************
 * Function: VEML6035_SET_SENS()
 * Description: Sets the sensitivity (SENS) bits in the ALS_CONF_0 register
 *  by reading the current register, masking out the SENS field and writing
 *  back the new value over I2C.
 * pramameter: Sens - sensitivity setting (VEML6035_SENS_0_x1 or
 *  VEML6035_SENS_1_x1_8)
 *  return: none
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
/***********************************************************************
 * Function: VEML6035_SET_DG()
 * Description: Sets the digital gain (DG) bits in the ALS_CONF_0 register
 *  by reading the current register, masking out the DG field and writing
 *  back the new value over I2C.
 * pramameter: DG - digital gain setting (VEML6035_DG_0_NORMAL or
 *  VEML6035_DG_1_DOUBLE)
 *  return: none
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
/***********************************************************************
 * Function: VEML6035_SET_GAIN()
 * Description: Sets the analog gain (GAIN) bits in the ALS_CONF_0 register
 *  by reading the current register, masking out the GAIN field and writing
 *  back the new value over I2C.
 * pramameter: Gain - analog gain setting (VEML6035_GAIN_0_NORMAL or
 *  VEML6035_GAIN_1_DOUBLE)
 *  return: none
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
/***********************************************************************
 * Function: VEML6035_SET_ALS_IT()
 * Description: Sets the ALS integration time bits in the ALS_CONF_0
 *  register; the integration time field spans both register bytes, so the
 *  high or low byte is masked and updated depending on which IT value is
 *  selected, then written over I2C.
 * pramameter: IntTime - integration time setting (VEML6035_ALS_IT_25ms
 *  through VEML6035_ALS_IT_800ms)
 *  return: none
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
/***********************************************************************
 * Function: VEML6035_SET_ALS_PERS()
 * Description: Sets the interrupt persistence (ALS_PERS) bits in the
 *  ALS_CONF_0 register by reading the current register, masking out the
 *  persistence field and writing back the new value over I2C.
 * pramameter: Pers - persistence setting (VEML6035_ALS_PERS_1,
 *  VEML6035_ALS_PERS_2, VEML6035_ALS_PERS_4 or VEML6035_ALS_PERS_8)
 *  return: none
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
/***********************************************************************
 * Function: VEML6035_SET_INT_CHANNEL()
 * Description: Selects which channel (ALS or White) triggers the interrupt
 *  by reading the ALS_CONF_0 register, masking out the interrupt-channel
 *  field and writing back the new value over I2C.
 * pramameter: Int_Channel - interrupt channel selection
 *  (VEML6035_ALS_CH_INT_EN or VEML6035_WHITE_CH_INT_EN)
 *  return: none
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
/***********************************************************************
 * Function: VEML6035_SET_CHANNEL_EN()
 * Description: Enables or disables the White channel by reading the
 *  ALS_CONF_0 register, masking out the White channel enable field and
 *  writing back the new value over I2C.
 * pramameter: White - White channel setting (VEML6035_WHITE_CH_DIS or
 *  VEML6035_WHITE_CH_EN)
 *  return: none
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
/***********************************************************************
 * Function: VEML6035_SET_INT_EN()
 * Description: Enables or disables the ALS interrupt by reading the
 *  ALS_CONF_0 register, masking out the interrupt enable field and writing
 *  back the new value over I2C.
 * pramameter: Interrupt - interrupt setting (VEML6035_ALS_INT_EN or
 *  VEML6035_ALS_INT_DIS)
 *  return: none
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
/***********************************************************************
 * Function: VEML6035_SET_SD()
 * Description: Turns the ALS sensor on or off by setting the shutdown (SD)
 *  bit in the ALS_CONF_0 register - reading the register, masking out the
 *  SD field and writing back the new value over I2C.
 * pramameter: SD_Bit - shutdown setting (VEML6035_ALS_SD_ON or
 *  VEML6035_ALS_SD_OFF)
 *  return: none
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
/***********************************************************************
 * Function: VEML6035_SET_ALS_HighThreshold()
 * Description: Writes the 16-bit high interrupt threshold value to the WH
 *  register over I2C, splitting it into low and high bytes.
 * pramameter: HighThreshold - high threshold value (0 to 65535)
 *  return: none
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
/***********************************************************************
 * Function: VEML6035_SET_ALS_LowThreshold()
 * Description: Writes the 16-bit low interrupt threshold value to the WL
 *  register over I2C, splitting it into low and high bytes.
 * pramameter: LowThreshold - low threshold value (0 to 65535)
 *  return: none
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
/***********************************************************************
 * Function: VEML6035_SET_PSM_WAIT()
 * Description: Sets the power saving mode waiting time bits in the PSM
 *  register by reading the current register, masking out the PSM_WAIT field
 *  and writing back the new value over I2C.
 * pramameter: PSM_Wait - waiting time setting (VEML6035_ALS_PSM_WAIT_0_4,
 *  _0_8, _1_6 or _3_2)
 *  return: none
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
/***********************************************************************
 * Function: VEML6035_SET_PSM_EN()
 * Description: Enables or disables power saving mode by setting the PSM_EN
 *  bit in the PSM register - reading the register, masking out the PSM_EN
 *  field and writing back the new value over I2C.
 * pramameter: PSM_En - power saving mode setting (VEML6035_ALS_PSM_EN or
 *  VEML6035_ALS_PSM_DIS)
 *  return: none
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
/***********************************************************************
 * Function: VEML6035_GET_ALS_DATA()
 * Description: Reads the 16-bit ALS measurement from the ALS data register
 *  over I2C and combines the high and low bytes into a single value.
 * pramameter: none
 *  return: Word ALS data count (0 to 65535)
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
/***********************************************************************
 * Function: VEML6035_GET_ALS_DATA_I2C_Res()
 * Description: Reads the 16-bit ALS measurement from the ALS data register
 *  over I2C, stores the combined value through the provided pointer and
 *  returns the I2C transfer status flag.
 * pramameter: RData - pointer to a Word that receives the ALS data count
 *  (0 to 65535)
 *  return: bool I2C read status (true on success, false on failure)
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
	// *RData = VEML6035_Data.RData;
	return flagI2C;
}

/*Read the White Channel Data
 *VEML6035_GET_WHITE_DATA() returns White Channel Data between 0d0 and 0d65535
 */
/***********************************************************************
 * Function: VEML6035_GET_WHITE_DATA()
 * Description: Reads the 16-bit White channel measurement from the WHITE
 *  data register over I2C and combines the high and low bytes into a single
 *  value.
 * pramameter: none
 *  return: Word White channel data count (0 to 65535)
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
/***********************************************************************
 * Function: VEML6035_GET_IF()
 * Description: Reads the interrupt flag (IF) register over I2C and returns
 *  the two high threshold/low threshold interrupt flag bits (mask 0xC0).
 * pramameter: none
 *  return: Byte interrupt flag bits (high/low threshold crossing status)
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
/***********************************************************************
 * Function: VEML6035_READ_Reg()
 * Description: Reads any sensor register specified by Reg over I2C and
 *  returns its 16-bit value with the high and low bytes combined.
 * pramameter: Reg - register address to read (e.g. VEML6035_ALS_CONF_0,
 *  VEML6035_WH, VEML6035_WL, VEML6035_PSM, VEML6035_ALS, VEML6035_WHITE,
 *  VEML6035_IF)
 *  return: Word the 16-bit register value (0x0000 to 0xFFFF)
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
/***********************************************************************
 * Function: VEML6035_GET_SD_Bit()
 * Description: Reads the ALS_CONF_0 register over I2C and returns the
 *  shutdown (SD) bit from the low byte (mask 0x01).
 * pramameter: none
 *  return: bool SD bit (0 = ALS channel on, 1 = shutdown)
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
/***********************************************************************
 * Function: VEML6035_GET_ALS_IT_Bits()
 * Description: Reads the ALS_CONF_0 register over I2C and decodes the
 *  integration time bits (spanning both register bytes) into a code from
 *  1 to 6.
 * pramameter: none
 *  return: int integration time code (1-6), or 0 if unrecognized
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
/***********************************************************************
 * Function: VEML6035_GET_GAIN_Bit()
 * Description: Reads the ALS_CONF_0 register over I2C and returns the
 *  analog GAIN bit from the high byte (mask 0x04).
 * pramameter: none
 *  return: bool GAIN bit (0 = normal sensitivity, 1 = double sensitivity)
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
/***********************************************************************
 * Function: VEML6035_GET_DG_Bit()
 * Description: Reads the ALS_CONF_0 register over I2C and returns the
 *  digital gain (DG) bit from the high byte (mask 0x08).
 * pramameter: none
 *  return: bool DG bit (0 = normal, 1 = double)
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
/***********************************************************************
 * Function: VEML6035_GET_SENS_Bit()
 * Description: Reads the ALS_CONF_0 register over I2C and returns the
 *  sensitivity (SENS) bit from the high byte (mask 0x10).
 * pramameter: none
 *  return: bool SENS bit (0 = high sensitivity x1, 1 = low sensitivity x1/8)
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
/***********************************************************************
 * Function: VEML6035_GET_PSM_EN_Bit()
 * Description: Reads the PSM register over I2C and returns the power saving
 *  mode enable (PSM_EN) bit from the low byte (mask 0x01).
 * pramameter: none
 *  return: bool PSM_EN bit (0 = power saving mode disabled, 1 = enabled)
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
/***********************************************************************
 * Function: VEML6035_GET_PSM_WAIT_Bits()
 * Description: Reads the PSM register over I2C and decodes the PSM waiting
 *  time bits (mask 0x06) into a code from 1 to 4.
 * pramameter: none
 *  return: int PSM_WAIT code (1-4), or 0 if unrecognized
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
/***********************************************************************
 * Function: VEML6035_GET_CHANNEL_EN_Bit()
 * Description: Reads the ALS_CONF_0 register over I2C and returns the
 *  channel enable bit from the low byte (mask 0x04).
 * pramameter: none
 *  return: bool channel enable bit (0 = ALS only / White disabled, 1 = ALS +
 *  White enabled)
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
