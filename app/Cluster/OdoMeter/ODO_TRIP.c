#include "ODO_TRIP.h"

#include "ODOMETER.h"
// #include "CALIBRATE.h"
// #include "YTM_EEPROM.h"

// #include "COMM_CAN_APPLY.h"
// #include "DIC_INFO.h"

// #include "COMMUNICATION.h"
// #include "printf.h"
#include "log.h"

uint32 TRIPA_VALUE;
uint32 TRIPA_VALUE_BAK;
uint16 TRIPA_PULSE_COUNTER,TRIPA_100M_COUNTER;
uint8 TRIPA_1KM_WRITE_REQUEST,TRIPA_100M_WRITE_REQUEST;
uint8 TRIPA_1KM_WRITE_COUNTER;
uint16 TRIPA_VALUE_CHECKSUM;

uint32 TRIPB_VALUE;
uint32 TRIPB_VALUE_BAK;
uint16 TRIPB_PULSE_COUNTER,TRIPB_100M_COUNTER;
uint8 TRIPB_1KM_WRITE_REQUEST,TRIPB_100M_WRITE_REQUEST;
uint8 TRIPB_1KM_WRITE_COUNTER;
uint16 TRIPB_VALUE_CHECKSUM;

//define
extern uint8 MENU_RETURN[]; 
//define


void ODO_TRIP_INIT_RESET(uint8 cold_boot)
{
	//if()
/*	uint16 temp,temp1;
	
	if(cold_boot)
	{ 	
	  if(TRIPA_VALUE>=TRIPA_VALUE_BAK)	temp=(TRIPA_VALUE-TRIPA_VALUE_BAK);
	  else temp=TRIPA_VALUE_BAK-TRIPA_VALUE;
	
	  temp1=TRIPA_VALUE+TRIPA_VALUE_BAK+1;
	  if(temp1>=TRIPA_VALUE_CHECKSUM)	temp1=temp1-TRIPA_VALUE_CHECKSUM;
	  else temp1=TRIPA_VALUE_CHECKSUM-temp;
	
	  if(temp>1 || temp1>3 || TRIPA_VALUE>9999 || TRIPA_VALUE_BAK>9999)  //9999.9km
	  {
		  ODO_TRIPA_CLEAR(); 
	  }
//-------------------------------------------------------------------	
	  if(TRIPB_VALUE>=TRIPB_VALUE_BAK)	temp=TRIPB_VALUE-TRIPB_VALUE_BAK;
	  else temp=TRIPB_VALUE_BAK-TRIPB_VALUE;
	
	  temp1=TRIPB_VALUE+TRIPB_VALUE_BAK+1;
	  if(temp1>=TRIPB_VALUE_CHECKSUM)	temp1=temp1-TRIPB_VALUE_CHECKSUM;
	  else temp1=TRIPB_VALUE_CHECKSUM-temp;
	
	  if(temp>1 || temp1>3 || TRIPB_VALUE>9999 || TRIPB_VALUE_BAK>9999)
	  {		
		  ODO_TRIPB_CLEAR(); 
	  }
	} */
  //-------------------------------------30reset
 // ODO_TRIP_CAL_READ();	
 
  	if(cold_boot) {   ODO_TRIPA_CLEAR(); ODO_TRIPB_CLEAR(); }  // 
    //	ODO_TRIP_CAL_READ();
}

void ODO_TRIP_INIT_IGN(void)
{
	TRIPA_1KM_WRITE_REQUEST=0;
	TRIPA_100M_WRITE_REQUEST=0;

	TRIPB_1KM_WRITE_REQUEST=0;
	TRIPB_100M_WRITE_REQUEST=0;

	TRIPA_1KM_WRITE_COUNTER=0;
	TRIPB_1KM_WRITE_COUNTER=0;
}

void ODO_TRIP_STANDBY(void)
{
	//if(DIC_IGNOFF_4H_FLAG==1) 	ODO_TRIPA_CLEAR(); 	
}

void ODO_TRIP_CAL_READ(void)	
{
	uint8 temp[4];
	uint32 short_temp;
		
	I2c_Eeprom_Read(ADDRESS_TRIPB,4,temp);
	short_temp=temp[3];	  short_temp=short_temp<<8;
	short_temp+=temp[2];	short_temp=short_temp<<8;
	short_temp+=temp[1];	short_temp=short_temp<<8;
	short_temp+=temp[0];
		
	
//	if(short_temp>99999) short_temp=0;
	
	if(2==MENU_RETURN[8]||3==MENU_RETURN[8]) 
  {
    if(short_temp>160932)	short_temp=0;
  } 
  else
  {
  	if(short_temp>99999)	short_temp=0;
  }

	TRIPB_VALUE=short_temp;
	
	TRIPB_VALUE_BAK=TRIPB_VALUE;
	TRIPB_VALUE_CHECKSUM=(uint8)(TRIPB_VALUE_BAK+TRIPB_VALUE+1);
	
//	TRIPA_VALUE=TRIPB_VALUE;
//	TRIPA_VALUE_BAK=TRIPA_VALUE;
//	TRIPA_VALUE_CHECKSUM=(uint8)(TRIPA_VALUE_BAK+TRIPA_VALUE+1);
}

//---------------------------------------------------------------------------------------
void ODO_TRIPA_PULSE_INC(void)
{
#if 0

	TRIPA_PULSE_COUNTER++;
	if(TRIPA_100M_COUNTER<10)
	{
		if(TRIPA_PULSE_COUNTER>=ODO_100M_PULSES_NUMBER_INT)
		{	
			TRIPA_PULSE_COUNTER=0;
			TRIPA_100M_COUNTER++;	ODO_TRIPA_INC_100M();  
		}
	}
	else
	{
		if(TRIPA_PULSE_COUNTER>=ODO_100M_PULSES_NUMBER_REM)
		{	
			TRIPA_PULSE_COUNTER=0;
			TRIPA_100M_COUNTER=0;	ODO_TRIPA_INC_100M();  
		}
	}	
#endif
}

void ODO_TRIPA_INC_100M(void)
{

  uint8 clear_falg=0;


	 if(2==MENU_RETURN[8]||3==MENU_RETURN[8]) 
  {
    if(TRIPA_VALUE>160932)	
    {
      clear_falg=1;
      IPK_RstTrip2_Send_Cnt=9;  IPK_RstTrip2=3; //ManualReset
    }
      
  } 
  else
  {
  	if(TRIPA_VALUE>99999)
    {
      clear_falg=1;
      IPK_RstTrip2_Send_Cnt=9;  IPK_RstTrip2=3; //ManualReset
    }
  }
  

	if(1!=clear_falg)	TRIPA_VALUE++;
	else
	{ 
    	TRIPA_VALUE=0;  
		// DIC_TIME_RESET_A();
		// DIC_FUEL_AVE_RESET_A();
		ODO_TRIPA_CLEAR();
		// DIC_AVG_SPD_A_RESET(); 
   }
	
/*  ODO_TRIPA_UPDATE_CHECKSUM();
  
	TRIPA_100M_WRITE_REQUEST=1;
	
	if(TRIPA_1KM_WRITE_COUNTER<10)  TRIPA_1KM_WRITE_COUNTER++;
	else 
	{
	  TRIPA_1KM_WRITE_COUNTER=0;
	  TRIPA_1KM_WRITE_REQUEST=1;
	} */
}

void ODO_TRIPA_CLEAR(void) //清除小计里程A
{
	if(TRIPA_VALUE!=0)  TRIPA_1KM_WRITE_REQUEST=1;

//	ODOMETER_TRIP_DIVEND_CLEAR();
	
	TRIPA_VALUE=0;
	TRIPA_PULSE_COUNTER=0;
	TRIPA_100M_COUNTER=0;
	
	  Trip_A_REMA=0;
  Trip_A_100Ms_M=0;

  TRIP_CLEAR_TXGC_FLAG=0;
  ODO_TRIPA_UPDATE_CHECKSUM();
}

void ODO_TRIPA_UPDATE_CHECKSUM(void) 
{
	TRIPA_VALUE_BAK=TRIPA_VALUE;
	TRIPA_VALUE_CHECKSUM=TRIPA_VALUE_BAK+TRIPA_VALUE+1;
}

void ODO_TRIPA_READ(void)	
{
/*	uint8 temp[2];
	uint16 short_temp;
		
	I2c_Eeprom_Read(ADDRESS_TRIPA,2,temp);
	short_temp=temp[1];	short_temp=(short_temp<<8)+temp[0];
	if(short_temp>9999) short_temp=0;
	TRIPA_VALUE=short_temp;
	
	TRIPA_VALUE_BAK=TRIPA_VALUE;
	TRIPA_VALUE_CHECKSUM=(uint8)(TRIPA_VALUE_BAK+TRIPA_VALUE+1);	 */
}

void ODO_TRIPA_WRITE(void)	
{
/*	uint8 temp[2];
	uint16 trip_a;
	
	trip_a=TRIPA_VALUE;
	temp[1]=(uint8)((trip_a>>8)&0xFF);
	temp[0]=(uint8)(trip_a&0xFF);
	
	EEPROM_WRITE_NO_DELAY(ADDRESS_TRIPA,2,temp); */
}
//---------------------------------------------------------------------------------------
void ODO_TRIPB_PULSE_INC(void)
{

  #if 0
  	TRIPB_PULSE_COUNTER++;
  	if(TRIPB_100M_COUNTER<10)
  	{
  		if(TRIPB_PULSE_COUNTER>=ODO_100M_PULSES_NUMBER_INT)
  	 	{	
  		 	TRIPB_PULSE_COUNTER=0;
  		 	TRIPB_100M_COUNTER++;	ODO_TRIPB_INC_100M();
  		}
  	}
  	else
  	{
  		if(TRIPB_PULSE_COUNTER>=ODO_100M_PULSES_NUMBER_REM)
  		{	
  		 	TRIPB_PULSE_COUNTER=0;
  		 	TRIPB_100M_COUNTER=0;	ODO_TRIPB_INC_100M();
  		}
  	}
  #endif

}

void ODO_TRIPB_INC_100M(void)
{
  if(2==MENU_RETURN[8]||3==MENU_RETURN[8]) 
  {
    if(TRIPB_VALUE<160932)	TRIPB_VALUE++;
  	else  { TRIPB_VALUE=0;  TRIPB_1KM_WRITE_REQUEST=1;  IPK_RstTrip1_Send_Cnt=9;  IPK_RstTrip1=2;  ODO_TRIPB_CLEAR();}
  } 
  else
  {
  	if(TRIPB_VALUE<99999)	TRIPB_VALUE++;
  	else  { TRIPB_VALUE=0;  TRIPB_1KM_WRITE_REQUEST=1;  IPK_RstTrip1_Send_Cnt=9;  IPK_RstTrip1=2; ODO_TRIPB_CLEAR();}
  }
	  
	ODO_TRIPB_UPDATE_CHECKSUM();
	
	TRIPB_100M_WRITE_REQUEST=1;
	
/*	if(TRIPB_1KM_WRITE_COUNTER<9)  TRIPB_1KM_WRITE_COUNTER++;
	else 
	{
	  TRIPB_1KM_WRITE_COUNTER=0;
	  TRIPB_1KM_WRITE_REQUEST=1;
	}	*/
}

void ODO_TRIPB_CLEAR(void)
{
   //	if(TRIPB_VALUE!=0)  TRIPB_1KM_WRITE_REQUEST=1;

	//ODOMETER_TRIP_DIVEND_CLEAR();

	TRIPB_VALUE=0;
	TRIPB_PULSE_COUNTER=0;
	TRIPB_100M_COUNTER=0;
	
	Trip_B_REMA=0;
  Trip_B_100Ms_M=0;

	ODO_TRIPB_UPDATE_CHECKSUM();
	
// 	DIC_TIME_RESET_B();
//   DIC_FUEL_AVE_RESET_B();
//   DIC_AVG_SPD_B_RESET();
  TRIP_CLEAR_TXGC_FLAG=0;
}

void ODO_TRIPB_UPDATE_CHECKSUM(void) 
{
	TRIPB_VALUE_BAK=TRIPB_VALUE;
	TRIPB_VALUE_CHECKSUM=TRIPB_VALUE_BAK+TRIPB_VALUE+1;
}

void ODO_TRIPB_READ(void)	
{
	uint8 temp[4];
	uint32 short_temp;
		
	I2c_Eeprom_Read(ADDRESS_TRIPB,4,temp);
	short_temp=temp[3];	  short_temp=short_temp<<8;
	short_temp+=temp[2];	short_temp=short_temp<<8;
	short_temp+=temp[1];	short_temp=short_temp<<8;
	short_temp+=temp[0];	
	
//	if(short_temp>99999) short_temp=0;

	
	 if(2==MENU_RETURN[8]||3==MENU_RETURN[8]) 
  {
    if(short_temp>160932)	short_temp=0;
  } 
  else
  {
  	if(short_temp>99999)	short_temp=0;
  }
  
  	TRIPB_VALUE=short_temp;
	
	TRIPB_VALUE_BAK=TRIPB_VALUE;
	TRIPB_VALUE_CHECKSUM=(uint8)(TRIPB_VALUE_BAK+TRIPB_VALUE+1);
}

void ODO_TRIPB_WRITE(void)	
{
	uint8 temp[4];
	uint32 trip_b;
	

	
	trip_b=TRIPB_VALUE;
	temp[3]=(uint8)((trip_b>>24)&0xFF);
	temp[2]=(uint8)((trip_b>>16)&0xFF);
	temp[1]=(uint8)((trip_b>>8)&0xFF);
	temp[0]=(uint8)(trip_b&0xFF);
	
	
//	I2c_Eeprom_Write(ADDRESS_TRIPB,4,temp);
}
extern uint8 POWER_GC_COMM_COUNTER;
void TRIP_Single_VALUE_100M(void) 
{
 	 if(TRIP_Single_VALUE<99999) TRIP_Single_VALUE++;
 	 else  TRIP_Single_VALUE=0;

   if(0==(TRIP_Single_VALUE%5)&&POWER_GC_COMM_COUNTER>20)
   {LOG_I("TRIP_Single_VALUE= %d",TRIP_Single_VALUE);} //500m once
}
