/**
 * @file    i2c.c
 * @brief   I2C peripheral driver implementation
 * @brief   I2C 外设驱动实现
 *
 * 模块级初始化以及（未来可能新增的）公共辅助函数均在本文件实现。
 * 取代了较早的 app/drv_api/i2c/i2c_init.c。
 */
#include "i2c.h"
#include "sdk_project_config.h"
#include "io.h"
#include "delay.h"

#define MOD_NAME  "I2C"
#include "log.h"

status_t I2c_Init(void)
{
    status_t status = STATUS_ERROR;

    I2c_Memory_Reset();
    status = I2C_DRV_MasterInit(0, &I2C_MasterConfig0, &I2C_MasterConfig0_State);
    return status;
}

/*breif 重置I2C总线，防止EEPROM死锁
*/
void I2c_Memory_Reset(void)
{
  uint8 counter;
  
	PORT_EEP_IIC_SDA_I;							//set data line input mode
	PORT_EEP_IIC_SCK_O;							//set clock line output mode

    while((counter<10) && (PORT_EEP_IIC_SDA_IS_H==0))			
    {										//memory reset
      PORT_EEP_IIC_SCK_L;
      delay_iic();
      PORT_EEP_IIC_SCK_H;
      delay_iic();
      counter++;
    }
        
        
	PORT_EEP_IIC_SCK_L;							//START
	PORT_EEP_IIC_SDA_H;
	PORT_EEP_IIC_SDA_O;							//set data line output mode
	PORT_EEP_IIC_SCK_H;
	delay_iic();
	PORT_EEP_IIC_SDA_L;
	delay_iic();                //STOP
	PORT_EEP_IIC_SCK_L;
	delay_iic();
	PORT_EEP_IIC_SCK_H;
	delay_iic();
	PORT_EEP_IIC_SDA_H;


	//恢复为初始化的状态
    Io_SetMuxModeSel(PCTRLA,2,PCTRL_MUX_ALT3); 
    Io_SetMuxModeSel(PCTRLA,3,PCTRL_MUX_ALT3); 
    PORT_EEP_IIC_SDA_L;
    PORT_EEP_IIC_SCK_L;

    Io_SetPinDirection(GPIOA,2,GPIO_INPUT_DIRECTION);
    Io_SetPinDirection(GPIOA,3,GPIO_INPUT_DIRECTION);
    GPIOA->PIER &= ~(1UL << 2);
    GPIOA->PIER &= ~(1UL << 3);
}


status_t I2c_Eeprom_Write(uint16 eep_address,uint8 wr_number,uint8* p_header)
{
	status_t status = STATUS_SUCCESS;
  	uint8 i2c_master_data[20] = {0};
  	uint8 retry_count = 10;
  
	if(wr_number > 16)
	{
		return status = STATUS_ERROR;
	}

	if(eep_address > 0x2FF)  
	{ 
		I2C_DRV_MasterSetSlaveAddr(0,0x53,false);
	}
	else if(eep_address > 0x1FF)
	{ 
		I2C_DRV_MasterSetSlaveAddr(0,0x52,false); 
	}
	else if(eep_address > 0xFF)
	{ 
		I2C_DRV_MasterSetSlaveAddr(0,0x51,false); 
	}
	else 
	{ 
		I2C_DRV_MasterSetSlaveAddr(0,0x50,false); 
	}
	
	i2c_master_data[0] = eep_address & 0xFF;
	for(uint16 i = 0; i < wr_number; i ++)
	{
		i2c_master_data[i+1] = p_header[i];
	}
	do
		{
			status = I2C_DRV_MasterSendDataBlocking(0, i2c_master_data, wr_number + 1, true, 1000);
			delay_10us(250);	
			delay_10us(250);	
			delay_10us(150);
		} while((status != STATUS_SUCCESS) && (retry_count--));

	return status;
}

status_t I2c_Eeprom_Read(uint16 eep_address,uint8 rd_number,uint8 * p_header)
{
	status_t status = STATUS_SUCCESS;
  	uint8 i2c_master_data[20] = {0};
	uint8 retry_count = 8;
  
	if(rd_number > 16)
	{
		return status = STATUS_ERROR;
	}

	if(eep_address > 0x2FF)  
	{ 
		I2C_DRV_MasterSetSlaveAddr(0,0x53,false); 
	}
	else if(eep_address > 0x1FF)
	{ 
		I2C_DRV_MasterSetSlaveAddr(0,0x52,false); 
	}
	else if(eep_address > 0xFF)
	{ 
		I2C_DRV_MasterSetSlaveAddr(0,0x51,false); 
	}
	else 
	{ 
		I2C_DRV_MasterSetSlaveAddr(0,0x50,false); 
	}
		
	
	i2c_master_data[0] = eep_address & 0xFF;
	do
		{
			status |= I2C_DRV_MasterSendDataBlocking(0, i2c_master_data, 1, false, 1000);
			status |= I2C_DRV_MasterReceiveDataBlocking(0, p_header , rd_number, true, 1000);
		} while((status != STATUS_SUCCESS) && (retry_count--));

	return status;
}

void I2c_3367_Byte_Write(uint8 data)
{
	uint8 mask;
	
	PORT_3367_IIC_SDA_O;							//set data line output mode
	PORT_3367_IIC_SCK_O;							//set clock line output mode	

	for(mask=0x80;mask!=0;mask=mask>>1)
	{
		if(data & mask) PORT_3367_IIC_SDA_H;
		else PORT_3367_IIC_SDA_L;
		
		delay_iic();
		
		PORT_3367_IIC_SCK_H;	
		delay_10us(5);
		PORT_3367_IIC_SCK_L;	
		delay_10us(5);
	}
	
	PORT_3367_IIC_SDA_I;							//set data line input mode
	
	delay_10us(5);
	
	PORT_3367_IIC_SCK_H;							//read ack
	delay_10us(5);
	PORT_3367_IIC_SCK_L;	
	
	PORT_3367_IIC_SDA_O;							//set data line output mode
}

void I2c_Data_3367_Read(uint16 eep_address,uint8 rd_number,uint8 * p_header)
{
	uint8 mask;
	uint8 temp_data;
	
	PORT_3367_IIC_SDA_O;							//set data line output mode
	PORT_3367_IIC_SCK_O;							//set clock line output mode
	PORT_3367_IIC_SDA_H;
	PORT_3367_IIC_SCK_H;

	delay_10us(5);
	
	PORT_3367_IIC_SDA_L;							//START
	delay_10us(5);
	PORT_3367_IIC_SCK_L;
	delay_10us(5);

	I2c_3367_Byte_Write(0x70); 
	I2c_3367_Byte_Write((uint8)eep_address);  
	
	PORT_3367_IIC_SDA_H;							//START
	delay_10us(5);
	PORT_3367_IIC_SCK_H;		
	delay_10us(5);
	PORT_3367_IIC_SDA_L;
	delay_10us(5);
	PORT_3367_IIC_SCK_L;		
	delay_10us(5);
	
	I2c_3367_Byte_Write(0x71);
		
	for(;rd_number!=0;rd_number--,p_header++)
	{
		temp_data=0;
		PORT_3367_IIC_SDA_I;						//set data line input mode
		
		for(mask=0x80;mask!=0;mask=mask>>1)
		{
			delay_10us(5);
			PORT_3367_IIC_SCK_H;
			if(PORT_3367_IIC_SDA_IS_H)	temp_data=temp_data | mask;
			delay_10us(5);
			PORT_3367_IIC_SCK_L;
		}
		
		*p_header=temp_data;

		if(rd_number!=1)
		{
			PORT_3367_IIC_SDA_O;					//ACK,set data line output mode
			PORT_3367_IIC_SDA_L;		
			delay_10us(5);
			PORT_3367_IIC_SCK_H;
			delay_10us(5);
			PORT_3367_IIC_SCK_L;
		}				
	}
	
	PORT_3367_IIC_SDA_O;							//NO ACK,set data line output mode
	PORT_3367_IIC_SDA_H;		
	delay_10us(5);
	PORT_3367_IIC_SCK_H;
	delay_10us(5);
	PORT_3367_IIC_SCK_L;
	delay_10us(5);
	
	PORT_3367_IIC_SDA_L;
	delay_10us(5);								//STOP
	PORT_3367_IIC_SCK_H;	
	delay_10us(5);
	PORT_3367_IIC_SDA_H;
}