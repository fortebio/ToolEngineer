/*
 * VEML6035.h
 *
 * Created  : 14 December 2021
 * Modified : 15 March 2022
 * Author   : HWanyusof
 * Version	: 1.2
 */
#include "define.h"

//Slave Address Device
#define VEML6035_Slave_Address	0x29

//Register Addresses (#ref.: Page 7 Table "Command Register Format" in the datasheet)
#define VEML6035_ALS_CONF_0		0x00
#define VEML6035_WH			    0x01
#define VEML6035_WL			    0x02
#define VEML6035_PSM			0x03
#define VEML6035_ALS			0x04
#define VEML6035_WHITE			0x05
#define VEML6035_IF				0x06

//Sensitivity Settings (#ref.: Page 7 Table 1 in the datasheet)
//Command Register: ALS_CONF_0
#define VEML6035_SENS_0_x1			0x00<<4
#define VEML6035_SENS_1_x1_8		0x01<<4

//Digital Gain Settings (#ref.: Page 7 Table 1 in the datasheet)
//Command Register: ALS_CONF_0
#define VEML6035_DG_0_NORMAL		0x00<<3
#define VEML6035_DG_1_DOUBLE		0x01<<3

//Gain Settings (#ref.: Page 7 Table 1 in the datasheet)
//Command Register: ALS_CONF_0
#define VEML6035_GAIN_0_NORMAL		0x00<<2
#define VEML6035_GAIN_1_DOUBLE		0x01<<2

//ALS Integration Time (#ref.: Page 7 Table 1 in the datasheet)
//Command Register: ALS_CONF_0
#define VEML6035_ALS_IT_25ms		0x03<<0
#define VEML6035_ALS_IT_50ms		0x02<<0
#define VEML6035_ALS_IT_100ms		0x00<<6
#define VEML6035_ALS_IT_200ms		0x01<<6
#define VEML6035_ALS_IT_400ms		0x02<<6
#define VEML6035_ALS_IT_800ms		0x03<<6

//ALS Persistence Protect number setting (#ref.: Page 7 Table 1 in the datasheet)
//Command Register: ALS_CONF_0
#define VEML6035_ALS_PERS_1			0x00<<4
#define VEML6035_ALS_PERS_2			0x01<<4
#define VEML6035_ALS_PERS_4			0x02<<4
#define VEML6035_ALS_PERS_8			0x03<<4

//Interrupt Channel setting (#ref.: Page 7 Table 1 in the datasheet)
//Command Register: ALS_CONF_0
#define VEML6035_ALS_CH_INT_EN		0x00<<3
#define VEML6035_WHITE_CH_INT_EN 	0x01<<3

//White Channel Enable/Disable (#ref.: Page 7 Table 1 in the datasheet)
//Command Register: ALS_CONF_0
#define VEML6035_WHITE_CH_DIS		0x00<<2
#define VEML6035_WHITE_CH_EN 		0x01<<2

//ALS Interrupt Enable/Disable (#ref.: Page 7 Table 1 in the datasheet)
//Command Register: ALS_CONF_0
#define VEML6035_ALS_INT_DIS 		0x00<<1
#define VEML6035_ALS_INT_EN			0x01<<1

//Set ALS 0n/Off (#ref.: Page 7 Table 1 in the datasheet)
//Command Register: ALS_CONF_0
#define VEML6035_ALS_SD_ON			0x00<<0
#define VEML6035_ALS_SD_OFF			0x01<<0

//Power Saving Mode (#ref.: Page 8 Table 4 in the datasheet)
//Command Register: PSM
#define VEML6035_ALS_PSM_WAIT_0_4	0x00<<1
#define VEML6035_ALS_PSM_WAIT_0_8	0x01<<1
#define VEML6035_ALS_PSM_WAIT_1_6	0x02<<1
#define VEML6035_ALS_PSM_WAIT_3_2	0x03<<1
#define VEML6035_ALS_PSM_DIS 		0x00<<0
#define VEML6035_ALS_PSM_EN			0x01<<0
