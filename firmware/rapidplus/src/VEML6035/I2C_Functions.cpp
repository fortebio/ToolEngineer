/*
 * I2C_Functions.cpp
 *
 * Created  : 14 December 2021
 * Modified : 15 March 2022
 * Author   : HWanyusof
 * Version	: 1.2
 */

#include "I2C_Functions.h"
#include "../define.h"
#include <stdio.h>
#include <stdlib.h>


//Arduino/Teensy/Adafruit or MCU that supports Arduino specific I2C API call

//All of the I2C API functions (For Example: Wire.write()) are being called from i2c_t3.h / Wire.h

//Master sends I2C write command via pointer *Data send from the Sensor API.
//The function returns 0 when no error and -1 when there is error.
int WriteI2C_Bus(struct TransferData *Data)
{
	//Initialization of intial Error = 0
	int Error = 0;
	//Arduino/Teensy/Adafruit could allow 2 parallel I2C bus to be used simultaneously.
	//Select_I2C_BUS is defined in Main_User.cpp.
	switch (Data->Select_I2C_Bus)
	{
		//Case I2C Bus 1
		case 1:
			//Send I2C Write Command as shown in Fig. 10 in the Datasheet Page 7.
			//Step 1) Initiate the Wire library and join the I2C bus as a master or slave. This should normally be called only once.
			Wire.begin();

			/*Step 2)  Send the 7-bit slave address to begin a transmission to the slave.
			*Wire.beginTransmission(uint8_t SlaveAddress) - Begin a transmission to the I2C slave device with the given address.
			*uint8_t SlaveAddress - the 7-bit address of the device to transmit to.
			*/
			Wire.beginTransmission(Data->Slave_Address);

			/*Step 3) Write data to the slave.
			*Wire.write(uint8_t value) - Writes data from a slave device in response to a request from a master, or queues bytes for transmission from a
			*master to slave device (in-between calls to beginTransmission() and endTransmission()).
			*uint8_t value - write 8-bit data like the register address to be written in the sensor and its corresponding 2 x 1 Byte values.
			*Sequence has to follow I2C Write Command as shown in Fig. 10 in the Datasheet Page 7 - Register Address, WData0, WData1.
			*/
			Wire.write(Data->RegisterAddress);
			Wire.write(Data->WData[0]);
			Wire.write(Data->WData[1]);

			//Step 4) Ends a transmission to a slave device that was begun by beginTransmission() and transmits the bytes that were queued by write().
			//Return error status (1 byte), which indicates the status of the transmission: 0 - success, !0 - there is error.
			Error = Wire.endTransmission();

			//Return -1 when there is error
			if(Error != 0)
			{
				return -1;
			}

			break;
		#ifdef i2ct3
			//Case I2C Bus 2
			case 2:
				//Send I2C Write Command as shown in Fig. 10 in the Datasheet Page 7.
				//Step 1) Initiate the Wire library and join the I2C bus as a master or slave. This should normally be called only once.
				Wire1.begin();

				/*Step 2)  Send the 7-bit slave address to begin a transmission to the slave.
				*Wire.beginTransmission(uint8_t SlaveAddress) - Begin a transmission to the I2C slave device with the given address.
				*uint8_t SlaveAddress - the 7-bit address of the device to transmit to.
				*/
				Wire1.beginTransmission(Data->Slave_Address);

				/*Step 3) Write data to the slave.
				*Wire.write(uint8_t value) - Writes data from a slave device in response to a request from a master, or queues bytes for transmission from a
				*master to slave device (in-between calls to beginTransmission() and endTransmission()).
				*uint8_t value - write 8-bit data like the register address to be written in the sensor and its corresponding 2 x 1 Byte values.
				*Sequence has to follow I2C Write Command as shown in Fig. 10 in the Datasheet Page 7 - Register Address, WData0, WData1.
				*/
				Wire1.write(Data->RegisterAddress);
				Wire1.write(Data->WData[0]);
				Wire1.write(Data->WData[1]);

				//Step 4) Ends a transmission to a slave device that was begun by beginTransmission() and transmits the bytes that were queued by write().
				//Return error status (1 byte), which indicates the status of the transmission: 0 - success, !0 - there is error.
				Error = Wire1.endTransmission();

				//Return -1 when there is error
				if(Error != 0)
				{
					return -1;
				}

				break;
		#endif

		//Other Cases I2C Bus set to default
		default:
			//Send I2C Write Command as shown in Fig. 10 in the Datasheet Page 7.
			//Step 1) Initiate the Wire library and join the I2C bus as a master or slave. This should normally be called only once.
			Wire.begin(SDA_Forte, SCL_Forte);		//update here in 5 Jan 2024

			/*Step 2) Send the 7-bit slave address to begin a transmission to the slave.
			*Wire.beginTransmission(uint8_t SlaveAddress) - Begin a transmission to the I2C slave device with the given address.
			*uint8_t SlaveAddress - the 7-bit address of the device to transmit to.
			*/
			Wire.beginTransmission(Data->Slave_Address);

			/*Step 3) Write data to the slave.
			*Wire.write(uint8_t value) - Writes data from a slave device in response to a request from a master, or queues bytes for transmission from a
			*master to slave device (in-between calls to beginTransmission() and endTransmission()).
			*uint8_t value - write 8-bit data like the register address to be written in the sensor and its corresponding 2 x 1 Byte values.
			*Sequence has to follow I2C Write Command as shown in Fig. 10 in the Datasheet Page 7 - Register Address, WData0, WData1.
			*/
			Wire.write(Data->RegisterAddress);
			Wire.write(Data->WData[0]);
			Wire.write(Data->WData[1]);

			//Step 4) Ends a transmission to a slave device that was begun by beginTransmission() and transmits the bytes that were queued by write().
			//Return error status (1 byte), which indicates the status of the transmission: 0 - success, !0 - there is error.
			Error = Wire.endTransmission();

			//Return -1 when there is error
			if(Error != 0)
			{
				return -1;
			}

			break;
	}
	return 0;
}

//Master sends I2C Read command and save the read data via pointer *Data.
//The function returns 0 when no error and -1 when there is error.
int ReadI2C_Bus(struct TransferData *Data)
{
	//Initialization of intial Error = 0
	int Error = 0;

	//Arduino/Teensy/Adafruit could allow 2 parallel I2C bus to be used simultaneously.
	//Select_I2C_BUS is defined in Main_User.cpp.
	switch (Data->Select_I2C_Bus)
	{

		//Case I2C Bus 1
		case 1:
			//Send I2C Read Command as shown in Fig. 10 in the Datasheet Page 7.
			//Step 1) Initiate the Wire library and join the I2C bus as a master or slave. This should normally be called only once.
			Wire.begin();

			/*Step 2) Send the 7-bit slave address to begin a transmission to the slave.
			*Wire.beginTransmission(uint8_t SlaveAddress) - Begin a transmission to the I2C slave device with the given address.
			*uint8_t SlaveAddress - the 7-bit address of the device to transmit to.
			*/
			Wire.beginTransmission(Data->Slave_Address);

			/*Step 3) Send the register address.
			*Wire.write(uint8_t value) - Writes data from a slave device in response to a request from a master, or queues bytes for transmission from a master *to slave device (in-between calls to beginTransmission() and endTransmission()).
			*uint8_t value - write 8-bit data like the register address to be written in the sensor.
			*/
			Wire.write(Data->RegisterAddress);

			/*Step 4) Send restart condition.
			*Wire.endTransmission(false) - With false as input, endTransmission() sends a restart message after transmission. The bus will not be released, which *prevents another master device from transmitting between messages. This allows one master device to send multiple transmissions while in control.
			*Return error status (1 byte), which indicates the status of the transmission: 0 - success, !0 - there is error.
			*/
			Error = Wire.endTransmission(false);

			//Return -1 when there is error
			if(Error != 0)
			{
				return -1;
			}

			/*Step 5) Request Data from slave.
			*Wire.requestFrom(uint8_t SlaveAddress, uint8_t Quantity, bool Stop) - Used by the master to request bytes from a slave device.
			*SlaveAddress: the 7-bit address of the device to request bytes from.
			*Quantity: the number of bytes to request. For Read Command, 2 Bytes are required.
			*Stop : boolean. true (true as declared in i2c_t3.h/Wire.h) will send a stop message after the request, releasing the bus.
			*/
			Wire.requestFrom(Data->Slave_Address, 2,true);

			/*Step 6) Read data from slave.
			*Wire.read() - Reads a byte that was transmitted from a slave device to a master after a call to requestFrom().
			*Read the 2 Bytes Data from the Sensor by calling it 2 times and store it in the RData Array.
			*/
			Data->RData[0] = Wire.read();
			Data->RData[1] = Wire.read();

			break;
		#ifdef i2ct3
			//Case I2C Bus 2
			case 2:
				//Send I2C Read Command as shown in Fig. 10 in the Datasheet Page 7.
				//Step 1) Initiate the Wire library and join the I2C bus as a master or slave. This should normally be called only once.
				Wire1.begin();

				/*Step 2) Send the 7-bit slave address to begin a transmission to the slave.
				*Wire.beginTransmission(uint8_t SlaveAddress) - Begin a transmission to the I2C slave device with the given address.
				*uint8_t SlaveAddress - the 7-bit address of the device to transmit to.
				*/
				Wire1.beginTransmission(Data->Slave_Address);

				/*Step 3) Send the register address.
				*Wire.write(uint8_t value) - Writes data from a slave device in response to a request from a master, or queues bytes for transmission from a master *to slave device (in-between calls to beginTransmission() and endTransmission()).
				*uint8_t value - write 8-bit data like the register address to be written in the sensor.
				*/
				Wire1.write(Data->RegisterAddress);

				/*Step 4) Send restart condition.
				*Wire.endTransmission(false) - With false as input, endTransmission() sends a restart message after transmission. The bus will not be released, which *prevents another master device from transmitting between messages. This allows one master device to send multiple transmissions while in control.
				*Return error status (1 byte), which indicates the status of the transmission: 0 - success, !0 - there is error.
				*/
				Error = Wire1.endTransmission(false);

				//Return -1 when there is error
				if(Error != 0)
				{
					return -1;
				}

				/*Step 5) Request Data from slave.
				*Wire.requestFrom(uint8_t SlaveAddress, uint8_t Quantity, bool Stop) - Used by the master to request bytes from a slave device.
				*SlaveAddress: the 7-bit address of the device to request bytes from.
				*Quantity: the number of bytes to request. For Read Command, 2 Bytes are required.
				*Stop : boolean. true (true as declared in i2c_t3.h/Wire.h) will send a stop message after the request, releasing the bus.
				*/
				Wire1.requestFrom(Data->Slave_Address, 2,true);

				/*Step 6) Read data from slave.
				*Wire.read() - Reads a byte that was transmitted from a slave device to a master after a call to requestFrom().
				*Read the 2 Bytes Data from the Sensor by calling it 2 times and store it in the RData Array.
				*/
				Data->RData[0] = Wire1.read();
				Data->RData[1] = Wire1.read();

				break;
		#endif

		//Other Cases I2C Bus set to default
		default:
			//Send I2C Read Command as shown in Fig. 10 in the Datasheet Page 7.
			//Step 1) Initiate the Wire library and join the I2C bus as a master or slave. This should normally be called only once.
			Wire.begin(14, 27);

			/*Step 2) Send the 7-bit slave address to begin a transmission to the slave.
			*Wire.beginTransmission(uint8_t SlaveAddress) - Begin a transmission to the I2C slave device with the given address.
			*uint8_t SlaveAddress - the 7-bit address of the device to transmit to.
			*/
			Wire.beginTransmission(Data->Slave_Address);

			/*Step 3) Send the register address.
			*Wire.write(uint8_t value) - Writes data from a slave device in response to a request from a master, or queues bytes for transmission from a master *to slave device (in-between calls to beginTransmission() and endTransmission()).
			*uint8_t value - write 8-bit data like the register address to be written in the sensor.
			*/
			Wire.write(Data->RegisterAddress);

			/*Step 4) Send restart condition.
			*Wire.endTransmission(false) - With false as input, endTransmission() sends a restart message after transmission. The bus will not be released, which *prevents another master device from transmitting between messages. This allows one master device to send multiple transmissions while in control.
			*Return error status (1 byte), which indicates the status of the transmission: 0 - success, !0 - there is error.
			*/
			Error = Wire.endTransmission(false);

			//Return -1 when there is error
			if(Error != 0)
			{
				return -1;
			}

			/*Step 5) Request Data from slave.
			*Wire.requestFrom(uint8_t SlaveAddress, uint8_t Quantity, bool Stop) - Used by the master to request bytes from a slave device.
			*SlaveAddress: the 7-bit address of the device to request bytes from.
			*Quantity: the number of bytes to request. For Read Command, 2 Bytes are required.
			*Stop : boolean. true (true as declared in i2c_t3.h/Wire.h) will send a stop message after the request, releasing the bus.
			*/
			// Wire.requestFrom(Data->Slave_Address, 2,true);
			uint8_t len = Wire.requestFrom(Data->Slave_Address, 2, true);
			
			if (len != 2)		//if the response length is not 2, this means it didn't read correctly, return err
			{
				return -1;
			}
			
			/*Step 6) Read data from slave.
			*Wire.read() - Reads a byte that was transmitted from a slave device to a master after a call to requestFrom().
			*Read the 2 Bytes Data from the Sensor by calling it 2 times and store it in the RData Array.
			*/
			Data->RData[0] = Wire.read();
			Data->RData[1] = Wire.read();

			break;
	}

	return 0;
}
