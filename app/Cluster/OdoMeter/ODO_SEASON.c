#include "ODO_SEASON.h"

#include "ODOMETER.h"
// #include "YTM_EEPROM.h"
// #include "CALIBRATE.h"
// #include "Zd_app.h"
// #include "COMM_CAN_APPLY.h"
#include "POWER_MODE.h"
// #include "TEL_LOGIC.h"

#include "ODO_TRIP.h"
// #include "DIC_INFO.h"
#include "signal.h"
#include "can_rx.h"
uint32 SEASON_VALUE;
uint32 SEASON_BAK1;
uint32 SEASON_BAK2;
uint8 SEASON_CHECKSUM;
uint32 Odo_Offset;

uint16 SEASON_PULSE_COUNTER;
uint8 SEASON_100M_COUNTER;

uint8 SEASON_ERR;
uint8 SEASON_1KM_WRITE_REQUEST;
uint8 SEASON_100M_WRITE_REQUEST;
uint8 SEASON_1KM_WRITE_COUNTER;

uint8 SEASON_BACKUP_ED;
uint32 Total,Offset;
uint32 SEASON_VALUE_offset;



//=============================================================================

void ODO_SEASON_INIT_RESET(uint8 cold_boot)
{
	if(cold_boot)
	{
		SEASON_PULSE_COUNTER=0;
		SEASON_100M_COUNTER=0;
		SEASON_1KM_WRITE_REQUEST=0;
		SEASON_100M_WRITE_REQUEST=0;
		SEASON_1KM_WRITE_COUNTER=0;
		Odo_Offset=0;
	}
}

void ODO_SEASON_INIT_IGN(void)
{
	SEASON_1KM_WRITE_REQUEST=0;
	SEASON_100M_WRITE_REQUEST=0;
	if(!ODO_SEASON_RAM_CHECK())	ODO_SEASON_READ();

	SEASON_BACKUP_ED=0;
}

void ODO_SEASON_STANDBY(void)
{
	//IPK_OdometerbackupEnable=0x0;
	Signal_Set(SIG_CAN_IPK_OdometerbackupEnable,0);
}

void ODO_SEASON_PULSE_INC(void)
{
#if 0
	SEASON_PULSE_COUNTER++;
	if(SEASON_100M_COUNTER<10)
	{
		if(SEASON_PULSE_COUNTER>=ODO_100M_PULSES_NUMBER_INT)
		{
			SEASON_100M_COUNTER++;
			ODO_SEASON_INC(); SEASON_PULSE_COUNTER=0;
		}
	}
	else
	{
		if(SEASON_PULSE_COUNTER>=ODO_100M_PULSES_NUMBER_REM)
		{
			SEASON_100M_COUNTER=0;
			ODO_SEASON_INC(); SEASON_PULSE_COUNTER=0;
		}
	}
#endif
}

void ODO_SEASON_INC(void)//????odo
{
  
  if(SEASON_VALUE<9999999)
  {                 
      	if(!SEASON_ERR)
      	{
      		if(!ODO_SEASON_RAM_CHECK())	ODO_SEASON_READ();
      		else if(SEASON_VALUE<9999999)
      		{
      			SEASON_VALUE++;
      			SEASON_BAK1=SEASON_VALUE;
      			SEASON_BAK2=SEASON_VALUE;			
      			SEASON_CHECKSUM=ODO_SEASON_GET_CHECKSUM();
      			
            SEASON_100M_WRITE_REQUEST=1;
            
            if(SEASON_1KM_WRITE_COUNTER<10) SEASON_1KM_WRITE_COUNTER++;
            else 
            {  
              SEASON_1KM_WRITE_COUNTER=0; 
              SEASON_1KM_WRITE_REQUEST=1;
            }
      		}
      	}
  }
}

uint8 ODO_SEASON_RAM_CHECK(void)
{
	if((SEASON_VALUE<9999999) && (SEASON_VALUE==SEASON_BAK1) && (SEASON_VALUE==SEASON_BAK2) && (SEASON_CHECKSUM==ODO_SEASON_GET_CHECKSUM()))
		return 1;
	else 
		return 0;
}

void ODO_SEASON_CLEAR(void)
{
  SEASON_VALUE=0;
  SEASON_BAK1=0;
	SEASON_BAK2=0;
	SEASON_ERR=0;
	SEASON_CHECKSUM=1;
	SEASON_1KM_WRITE_REQUEST=1;
	
//   DIC_TIME_RESET_A();
//   DIC_FUEL_AVE_RESET_A();
  ODO_TRIPA_CLEAR();
//   DIC_AVG_SPD_A_RESET();
  
//   DIC_TIME_RESET_B();
//   DIC_FUEL_AVE_RESET_B();
  ODO_TRIPB_CLEAR();
//   DIC_AVG_SPD_B_RESET();
//   //IPK_RstTrip1_Send_Cnt=9;  IPK_RstTrip1=7; //RstTrip1_RstTripCompterDataRstALL
}

uint32 ODO_SEASON_READ_CHECK(void)
{
 	uint32 SEASON_EEPROM1;
 	uint32 SEASON_EEPROM2;
 	uint32 SEASON_EEPROM3;
	uint8 counter,i;
	uint8 temp[4];
	
	for(counter=0;counter<=3;counter++)			//if find any error,read again,total 3 times.
	{
		for(i=0;i<4;i++)	temp[i]=0;
		EEPROM_READ(0x10,4,temp);
		SEASON_EEPROM1=temp[3];		SEASON_EEPROM1=SEASON_EEPROM1<<8;
		SEASON_EEPROM1+=temp[2];	SEASON_EEPROM1=SEASON_EEPROM1<<8;
		SEASON_EEPROM1+=temp[1];	SEASON_EEPROM1=SEASON_EEPROM1<<8;
		SEASON_EEPROM1+=temp[0];

		for(i=0;i<4;i++)	temp[i]=0;
		EEPROM_READ(0x14,4,temp);
		SEASON_EEPROM2=temp[3];		SEASON_EEPROM2=SEASON_EEPROM2<<8;
		SEASON_EEPROM2+=temp[2];	SEASON_EEPROM2=SEASON_EEPROM2<<8;
		SEASON_EEPROM2+=temp[1];	SEASON_EEPROM2=SEASON_EEPROM2<<8;
		SEASON_EEPROM2+=temp[0];

		for(i=0;i<4;i++)	temp[i]=0;
		EEPROM_READ(0x18,4,temp);
		SEASON_EEPROM3=temp[3];		SEASON_EEPROM3=SEASON_EEPROM3<<8;
		SEASON_EEPROM3+=temp[2];	SEASON_EEPROM3=SEASON_EEPROM3<<8;
		SEASON_EEPROM3+=temp[1];	SEASON_EEPROM3=SEASON_EEPROM3<<8;
		SEASON_EEPROM3+=temp[0];

		if((SEASON_EEPROM1<16093451) && (SEASON_EEPROM1==SEASON_EEPROM2) && (SEASON_EEPROM1==SEASON_EEPROM3))
			return SEASON_EEPROM1;
	}
	
	if((SEASON_EEPROM1==SEASON_EEPROM2) && (SEASON_EEPROM1<16093451))
		return SEASON_EEPROM1;

	else if((SEASON_EEPROM1==SEASON_EEPROM3) && (SEASON_EEPROM1<16093451))
		return SEASON_EEPROM1;

	else if((SEASON_EEPROM2==SEASON_EEPROM3) && (SEASON_EEPROM2<16093451))
		return SEASON_EEPROM2;

	else if(((SEASON_EEPROM1>>8)==(SEASON_EEPROM2>>8)) && (SEASON_EEPROM1<16093451))
		return SEASON_EEPROM1;

	else if(((SEASON_EEPROM1>>8)==(SEASON_EEPROM3>>8)) && (SEASON_EEPROM1<16093451))
		return SEASON_EEPROM1;

	else if(((SEASON_EEPROM2>>8)==(SEASON_EEPROM3>>8)) && (SEASON_EEPROM2<16093451))
		return SEASON_EEPROM2;
	
	else 
		return 0xFFFFFFFF;
}


void ODO_SEASON_READ(void)
{
 	uint32 temp;
 	
 	temp=ODO_SEASON_READ_CHECK();
 	if(temp!=0xFFFFFFFF)
 	{
		SEASON_VALUE=temp;
	 	SEASON_BAK1=temp;
	 	SEASON_BAK2=temp;
		SEASON_CHECKSUM=ODO_SEASON_GET_CHECKSUM();
		SEASON_ERR=0;
 	}
 	else
 	{
 		if(!ODO_SEASON_RAM_CHECK())	SEASON_ERR=1;
 	}
}

void ODO_SEASON_WRITE(uint8 index)
{	
	uint32 season;
	uint8 temp[4];
	
	if(!SEASON_ERR)
	{
		if(ODO_SEASON_RAM_CHECK())
		{
			season=SEASON_VALUE;
			temp[3]=(uint8)((season>>24)&0xFF);
			temp[2]=(uint8)((season>>16)&0xFF);
			temp[1]=(uint8)((season>>8)&0xFF);
			temp[0]=(uint8)(season&0xFF);
				
			if(index==1)			EEPROM_WRITE_NO_DELAY(0x10,4,temp);    //???????¡§????¡Á????¨º?¡§????
			else if(index==2)	EEPROM_WRITE_NO_DELAY(0x14,4,temp);
			else if(index==3)	EEPROM_WRITE_NO_DELAY(0x18,4,temp);
		}
		else	ODO_SEASON_READ();
	}
}

uint8 ODO_SEASON_GET_CHECKSUM(void)
{
	return(1+(uint8)(SEASON_VALUE)+(uint8)(SEASON_VALUE>>8)+(uint8)(SEASON_VALUE>>16));
}

void ODO_SEASON_DIAG_SETUP(uint32 season)
{
	SEASON_VALUE=season;
	SEASON_BAK1=season;
	SEASON_BAK2=season;
	SEASON_ERR=0;
	SEASON_CHECKSUM=ODO_SEASON_GET_CHECKSUM();
	SEASON_1KM_WRITE_REQUEST=1;
}

uint8 Get_IPK_OdometerbackupEnable(void)
{
#if OdometerbackupEnable//C02_B2????????
   return  IPK_OdometerbackupEnable;
#else
	return 0;
#endif
}

void ODO_SEASON_BACKUP_TASK(void)
{
#if OdometerbackupEnable//C02_B2????????
	uint8 backup_req=0;
	uint8 temp[8];
	uint32 bcm_odo,ems_odo,backup_value,delta,season;

//---------------------------------------------------------
	if(!F101_Fun_Cfg.Odo_Backup)	IPK_OdometerbackupEnable=0;
	else if(SEASON_ERR)	IPK_OdometerbackupEnable=0;
	else if(EMS_EngineDriverInfo_TIMEOUT || BCM_SunroofState_TIMEOUT)	IPK_OdometerbackupEnable=0;
	else if((!EMS_Odometerbackup_ENABLE)||(!BCM_Odometerbackup_ENABLE))	IPK_OdometerbackupEnable=0;
	else IPK_OdometerbackupEnable=1;
//---------------------------------------------------------
	if(F101_Fun_Cfg.Odo_Backup && IPK_OdometerbackupEnable && (POWER_IGN_COUNTER>2) && (!SEASON_BACKUP_ED))
	{
		if((EMS_Odometerbackup==0)||(EMS_Odometerbackup==0x3FFF))	{}
		else if((BCM_Odometerbackup==0)||(BCM_Odometerbackup==0x3FFF))	{}
		else if((EMS_Odometerbackup_CNT==5)&&(BCM_Odometerbackup_CNT==5))
		{
			season=SEASON_VALUE/10;
			ems_odo=EMS_Odometerbackup;	ems_odo*=100;
			bcm_odo=BCM_Odometerbackup;	bcm_odo*=100;
			if(ems_odo>=bcm_odo)
			{
				delta=ems_odo-bcm_odo;
				if(delta<=300)
				{
					if(ems_odo>=season)	delta=ems_odo-season;
					else delta=season-ems_odo;
					if(delta>300)	{	backup_req=1;	backup_value=ems_odo;	}
				}
			}
			else
			{
				delta=bcm_odo-ems_odo;
				if(delta<=300)
				{
					if(bcm_odo>=season)	delta=bcm_odo-season;
					else delta=season-bcm_odo;
					if(delta>300)	{	backup_req=1;	backup_value=bcm_odo;	}
				}
			}
//---------------------------------------------------------
			if(backup_req)
			{
				if(backup_value>999999)	backup_value=999999;
				backup_value*=10;
				ODO_SAVE_OFFSET(backup_value);

				SEASON_VALUE=backup_value;
				SEASON_BAK1=SEASON_VALUE;
				SEASON_BAK2=SEASON_VALUE;
				SEASON_CHECKSUM=ODO_SEASON_GET_CHECKSUM();
				SEASON_1KM_WRITE_REQUEST=1;
				SEASON_BACKUP_ED=1;
				CAL_ODO_BAK_CNT_INC();
				//Total=ODO_GET_TOTAL();
//---------------------------------------------------------
				temp[0]=IPK_Year;
				temp[1]=IPK_Month;
				temp[2]=IPK_Day;
				temp[3]=IPK_Hour;
				temp[4]=(uint8)((backup_value>>0)&0xFF);
				temp[5]=(uint8)((backup_value>>8)&0xFF);
				temp[6]=(uint8)((backup_value>>16)&0xFF);
				temp[7]=(uint8)((backup_value>>24)&0xFF);
				//REFRESH_WATCHDOG( );						// Refresh watchdog
				EEPROM_WRITE(ADDRESS_ODO_BACKUP_SNAPSHOT,8,temp);
			}
		}
	}
#endif
}

void ODO_SAVE_OFFSET(uint32 tar)
{
	uint8 temp[4];
	uint32 offset,delta;

/*
	EEPROM_READ(ADDRESS_OFFSET_ODO,4,temp);
	offset=temp[3];		offset=offset<<8;
	offset+=temp[2];	offset=offset<<8;
	offset+=temp[1];	offset=offset<<8;
	offset+=temp[0];
	if(offset==0xFFFFFFFF)	offset=0;
	*/

	SEASON_VALUE_offset=ODO_GET_TOTAL_offset();
	offset =SEASON_VALUE_offset;

	if(offset&0x80000000)		// offset<0
	{
		offset&=0x7FFFFFFF;

		if(tar>=SEASON_VALUE)
		{
			delta=tar-SEASON_VALUE;
			if(offset>delta)	{	offset-=delta;	offset|=0x80000000;	}
			else offset=SEASON_VALUE-offset;
		}
		else
		{
			offset+=SEASON_VALUE-tar;
			offset|=0x80000000;
		}
	}
	else
	{
		if(tar>=SEASON_VALUE)	offset+=tar-SEASON_VALUE;
		else
		{
			delta=SEASON_VALUE-tar;
			if(offset>=delta)	offset-=delta;
			else
			{
				offset=delta-offset;
				offset|=0x80000000;
			}
		}
	}

	SEASON_VALUE_offset=offset;

	temp[0]=(uint8)((offset>>0)&0xFF);
	temp[1]=(uint8)((offset>>8)&0xFF);
	temp[2]=(uint8)((offset>>16)&0xFF);
	temp[3]=(uint8)((offset>>24)&0xFF);

	//REFRESH_WATCHDOG( );						// Refresh watchdog
	EEPROM_WRITE(ADDRESS_OFFSET_ODO,4,temp);
}

uint32 ODO_GET_TOTAL_offset(void)
{
	uint8 temp[4];
	uint32 offset;

	EEPROM_READ(ADDRESS_OFFSET_ODO,4,temp);
	offset=temp[3]; 	offset=offset<<8;
	offset+=temp[2];	offset=offset<<8;
	offset+=temp[1];	offset=offset<<8;
	offset+=temp[0];
	if(offset==0xFFFFFFFF)	offset=0;
	return offset;
}

uint32 ODO_GET_TOTAL(void)
{
	//uint8 temp[4];
	uint32 total,offset;

/*
	EEPROM_READ(ADDRESS_OFFSET_ODO,4,temp);
	offset=temp[3];		offset=offset<<8;
	offset+=temp[2];	offset=offset<<8;
	offset+=temp[1];	offset=offset<<8;
	offset+=temp[0];
	if(offset==0xFFFFFFFF)	offset=0;
*/
	offset =SEASON_VALUE_offset;
	Offset=offset;

	if(SEASON_ERR)	total=0xFFFFFFFF;
	if(offset>0xF0000000)	total=0xFFFFFFFF;
	else
	{
		if(offset&0x80000000)			// offset<0
		{
			offset&=0x7FFFFFFF;
			total=SEASON_VALUE+offset;
			if(total>9999999)	total=0xFFFFFFFF;
		}
		else
		{
			if(SEASON_VALUE>=offset)	total=SEASON_VALUE-offset;	else total=0xFFFFFFFF;
		}
	}

	return total;
}

