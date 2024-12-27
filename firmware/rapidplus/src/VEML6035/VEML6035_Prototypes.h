/*
 * VEML6035_Prototypes.h
 *
 * Created  : 14 December 2021
 * Modified : 15 March 2022
 * Author   : HWanyusof
 * Version	: 1.2
 */

#include "typedefinition.h"


/*Set the Sensitivity
 *VEML6035_SET_SENS(Byte Sens)
 *Byte Sens - Input Parameter:
 *
 * VEML6035_SENS_0_x1
 * VEML6035_SENS_1_x1_8
 */
void VEML6035_SET_SENS(Byte Sens);

/*Set the Digital Gain (DG)
 *VEML6035_SET_DG(Byte DG)
 *Byte DG - Input Parameter:
 *
 * VEML6035_DG_0_NORMAL
 * VEML6035_DG_1_DOUBLE
 */
void VEML6035_SET_DG(Byte DG);

/*Set the Gain
 *VEML6035_SET_GAIN(Byte Gain)
 *Byte Gain - Input Parameter:
 *
 * VEML6035_GAIN_0_NORMAL
 * VEML6035_GAIN_1_DOUBLE
 */
void VEML6035_SET_GAIN(Byte Gain);

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
void VEML6035_SET_ALS_IT(Byte IntTime);

/*Set the Persistence
 *VEML6035_SET_ALS_PERS(Byte Pers)
 *Byte Pers - Input Parameter:
 *
 * VEML6035_ALS_PERS_1
 * VEML6035_ALS_PERS_2
 * VEML6035_ALS_PERS_4
 * VEML6035_ALS_PERS_8
 */
void VEML6035_SET_ALS_PERS(Byte Pers);

/*Set the Interrupt Channel
 *VEML6035_SET_INT_CHANNEL(Byte Int_Channel)
 *Byte Int_Channel - Input Parameter:
 *
 * VEML6035_ALS_CH_INT_EN
 * VEML6035_WHITE_CH_INT_EN
 */
void VEML6035_SET_INT_CHANNEL(Byte Int_Channel);

/*Enable/Disable White Channel
 *VEML6035_SET_CHANNEL_EN(Byte White)
 *Byte White - Input Parameter:
 *
 * VEML6035_WHITE_CH_DIS
 * VEML6035_WHITE_CH_EN
 */
void VEML6035_SET_CHANNEL_EN(Byte White);

/*Enable/Disable Interrupt
 *VEML6035_SET_INT_EN(Byte Interrupt)
 *Byte Interrupt - Input Parameter:
 *
 * VEML6035_ALS_INT_EN
 * VEML6035_ALS_INT_DIS
 */
void VEML6035_SET_INT_EN(Byte Interrupt);

/*Turn On/Off the ALS Sensor
 *VEML6035_SET_SD(Byte SD_Bit)
 *Byte SD_Bit - Input Parameter:
 *
 * VEML6035_ALS_SD_ON
 * VEML6035_ALS_SD_OFF
 *
 */
void VEML6035_SET_SD(Byte SD_Bit);

/*Set the High Threshold
 *VEML6035_SET_ALS_HighThreshold(Word HighThreshold);
 *Word HighThreshold - Input Parameter:
 *
 * Value between 0d0 and 0d65535
 */
void VEML6035_SET_ALS_HighThreshold(Word HighThreshold);

/*Set the Low threshold
 *VEML6035_SET_ALS_LowThreshold(Word LowThreshold)
 *Word LowThreshold - Input Parameter:
 *
 * Value between 0d0 and 0d65535
 */
void VEML6035_SET_ALS_LowThreshold(Word LowThreshold);

/*Set Power Saving Mode Waiting Time
 *VEML6035_SET_PSM_WAIT(Byte PSM_Wait)
 *Byte PSM_Wait - Input Parameter:
 *
 * VEML6035_ALS_PSM_WAIT_0_4
 * VEML6035_ALS_PSM_WAIT_0_8
 * VEML6035_ALS_PSM_WAIT_1_6
 * VEML6035_ALS_PSM_WAIT_3_2
 */
void VEML6035_SET_PSM_WAIT(Byte PSM_Wait);

/*Enable/Disable the Power Saving Mode
 *VEML6035_SET_PSM_EN(Byte PSM_En)
 *Byte PSM_En - Input Parameter:
 *
 * VEML6035_ALS_PSM_EN
 * VEML6035_ALS_PSM_DIS
 */
void VEML6035_SET_PSM_EN(Byte PSM_En);

/*Read the ALS Data
 *VEML6035_GET_ALS_DATA() returns ALS Data between 0d0 and 0d65535
 */
Word VEML6035_GET_ALS_DATA();

/*Read the ALS Data with err status return
 *VEML6035_GET_ALS_DATA() returns I2C reading status, with ALS Data between 0d0 and 0d65535 transferred by function parameter
 */
bool VEML6035_GET_ALS_DATA_I2C_Res(Word * RData);

/*Read the White Channel Data
 *VEML6035_GET_WHITE_DATA() returns White Channel Data between 0d0 and 0d65535
 */
Word VEML6035_GET_WHITE_DATA();

/*Read the ALS Interrupt Flag
 *VEML6035_GET_IF() returns interrupt flag status.
 *Please refer to Table 7 in the Datasheet page 8
 */
Byte VEML6035_GET_IF();

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
Word VEML6035_READ_Reg(Byte Reg);

/*Read the SD bit
 *returns 0 for ALS channel on and 1 for shutdown
 */
bool VEML6035_GET_SD_Bit();

/*Read the ALS_IT bits
 *returns 0d0 - 0d6 depending on ALS_IT bits
 */
int VEML6035_GET_ALS_IT_Bits();

/*Read the GAIN bit
 *returns 0 for normal sensitivity and 1 for double sensitivity
 */
bool VEML6035_GET_GAIN_Bit();

/*Read the DG bit
 *returns 0 for normal and 1 for double
 */
bool VEML6035_GET_DG_Bit();

/*Read the SENS bit
 *returns 0 for high sensitivity (1x) and 1 for low sensitivity (1/8x)
 */
bool VEML6035_GET_SENS_Bit();

/*Read the PSM_EN bit
 *returns 0 for Power Saving Mode Disable and 1 for Power Saving Mode Enable
 */
bool VEML6035_GET_PSM_EN_Bit();

/*Read the PSM_WAIT bit
 *returns 0d0 - 0d4 depending on PSM_WAIT bits
 */
int VEML6035_GET_PSM_WAIT_Bits();

/*Read the CHANNEL_EN bit
 *returns 0 for ALS channel enable (Disable White channel) and 1 for ALS + White channel enable
 */
bool VEML6035_GET_CHANNEL_EN_Bit();
