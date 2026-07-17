//*****************************************************************************
//	CONFIDENTIAL
// 	Copyright (C) 2014 SH.ETC. All rights reserved.
// 	Module			: $ ODOMETER.c $
// 	Description	: OdoMeter Entrance
//								include total odometer(season) and short odometer(trip)
// 	Version			: $ Rev: 1.0 $
// 	Last UpDate Time: $Date:: 2021-03-23 16:20:46 $
// 	FOOT NOTE		: TBD
// 	AUTHOR			: SH.ETC.
//*****************************************************************************

#include "ODOMETER.h"

#include "ODO_TRIP.h"
#include "ODO_SEASON.h"
// #include "SPD_INPUT.h"
#include "POWER_MODE.h"
// #include "YTM_EEPROM.h"
// #include "DIC_INFO.h"
// #include "METER.h"
// #include "TAC_INPUT.h"
// #include "CALIBRATE.h"
// #include "TEL_COMM_INPUT.h"
// #include "COMM_CAN_APPLY.h"
// #include "DIC_INFO.h"
// #include "METER.h"
// #include "YTM_CONFIGURE.h"
#include "signal.h"
#include "can_rx.h"
#include "scheduler.h"
#include "rti.h"

#define MOD_NAME  "ODO"
#include "log.h"

//#include "DIC_BUTTON.h"

uint16 ODO_100M_PULSES_NUMBER_INT;	// 100m pulses number integer 	e.g. ppk=3822,it=382
uint16 ODO_100M_PULSES_NUMBER_REM;  // 100m pulses number remainder e.g. ppk=3822,it=384

uint8 ODO_WRITE_MODE;							// odometer write int eeprom mode

uint16 SEASON_100MS_REMAINDER;
uint16 SEASON_100MS_M;
uint8 SEASON_100MS_COUNTER;
uint8 ODOClear_times;

uint8 EMS_EngineOdometerCounter_OLD;
uint8 ENGINE_ODO_METER_COUNTER_INPUT;
uint16 ENGINE_ODO_METER_COUNTER_INPUT_VALUE;
T_FLAG8   ODO_CLEAR_REQUEST;

uint16 SEASON_20MS_REMAINDER;
uint16 SEASON_20MS_M;
uint8 SEASON_20MS_COUNTER;

uint16  Trip_A_REMA,Trip_A_100Ms_M;
uint16  Trip_B_REMA,Trip_B_100Ms_M;

//define
void EEPROM_READ(uint16 eep_address,uint8 rd_number,uint8 * p_header)
{

}
void EEPROM_WRITE_NO_DELAY(uint16 eep_address,uint8 wr_number,uint8* p_header)
{

}
void EEPROM_WRITE(uint16 eep_address,uint8 wr_number,uint8* p_header)
{

}
void TEL_COMM_SERVICE_WRITE(uint8 diag)
{

}
void TEL_COMM_SERVICE_DAY_WRITE(uint8 diag)
{

}
uint8  TRIP_CLEAR_TXGC_FLAG=0;

uint8 IPK_RstTrip2=0;
uint8 IPK_RstTrip2_Send_Cnt=0;
uint8 IPK_RstTrip1=0;
uint8 IPK_RstTrip1_Send_Cnt=0;
uint32 TRIP_Single_VALUE;
uint32 SPD_RATE;
uint8 EEPROM_WRITE_BUSY=0;
uint8 TEL_SERVICE_DAY_WRITE_REQ=1;
uint16 SPD_INPUT_VALUE;   //0.1km/h
uint8 SPD_INPUT_MODE=SPD_CAN_MODE;
uint16  Trip_Single_REMA,Trip_Single_100Ms_M;
uint16  PRINT_UINT_100m_ADD_CNT; 
uint32 FUEL_INC_ODO;
//define

//*****************************************************************************
// FunName: ODOMETER_INIT_RESET
// Desc.	:	do initialization when MCU reset
// inputs :	cold_boot : 1 cold boot,0 warm boot 
// outputs: none
//*****************************************************************************
void ODOMETER_INIT_RESET(uint8 cold_boot)
{
	ODO_TRIP_INIT_RESET(cold_boot);
	ODO_SEASON_INIT_RESET(cold_boot);

	ODO_WRITE_MODE=0;

	SEASON_100MS_REMAINDER=0;
	SEASON_100MS_M=0;

	ODO_CLEAR_REQUEST.by=0;

  Trip_A_REMA=0;
  Trip_A_100Ms_M=0;
  Trip_B_REMA=0;
  Trip_B_100Ms_M=0;
}

//*****************************************************************************
// FunName: ODOMETER_INIT_IGN
// Desc.	:	do initialization when ign on
// inputs : none
// outputs: none
//*****************************************************************************
void ODOMETER_INIT_IGN(void)
{
	ODO_TRIP_INIT_IGN();
	ODO_SEASON_INIT_IGN();

	#if(TRIP_SAVE_ACTIVE)
	{
		ODO_TRIPA_READ();
		#if(TRIPB_ACTIVE)
		{
		
		}
		#endif
	}
	#endif
	
//		ODO_TRIPB_READ();

  ODO_WRITE_MODE=0;

	ODO_100M_PULSES_NUMBER_INT=(uint16)(SPD_RATE/10);
	ODO_100M_PULSES_NUMBER_REM=(uint16)(SPD_RATE/10+SPD_RATE%10);

	SEASON_100MS_COUNTER=0;
	ODO_CLEAR_REQUEST_CAN=0;

	SEASON_20MS_REMAINDER=0;
	SEASON_20MS_M=0;
	SEASON_20MS_COUNTER=0;
}

//*****************************************************************************
// FunName: ODOMETER_STANDBY
// Desc.	:	do perpares before standby when ign off
// inputs : none
// outputs: none
//*****************************************************************************
void ODOMETER_STANDBY(void) 
{
  ODO_TRIP_STANDBY();
  ODO_SEASON_STANDBY();
}

void ODOMETER_TRIP_DIVEND_CLEAR(void)
{
	SEASON_100MS_REMAINDER=0;
	SEASON_100MS_M=0;
	SEASON_100MS_COUNTER=0;
}

//*****************************************************************************
// FunName: ODO_PULSE_INC
// Desc.	:	in ododmeter pulse mode,call when speed sensor pulse inc by 1
// inputs : none
// outputs: none
//*****************************************************************************
void ODO_PULSE_INC(void) 
{
	ODO_SEASON_PULSE_INC();
	ODO_TRIPA_PULSE_INC();
	#if(TRIPB_ACTIVE)
		ODO_TRIPB_PULSE_INC();
	#endif
}

//*****************************************************************************
// FunName: ODO_WRITE_TASK
// Desc.	:	write ododmeter to EEPROM,call periodic(10ms default)
// inputs : none
// outputs: none
//*****************************************************************************
void ODO_WRITE_TASK(void) 
{
//-------------------------------------------------------------------
	if(POWER_IGN_ON)
	{
		ODO_TRIP_SEASON_CHECK();
		ODO_CLEAR_TASK();
	}
	else
	{
		if(SEASON_100M_WRITE_REQUEST)
		{	SEASON_100M_WRITE_REQUEST=0;	SEASON_1KM_WRITE_REQUEST=1;	}

		#if(TRIP_SAVE_ACTIVE)
		{
			if(TRIPA_100M_WRITE_REQUEST)
			{	TRIPA_100M_WRITE_REQUEST=0;	TRIPA_1KM_WRITE_REQUEST=1;	}


		}
		#endif
		
			if(TRIPB_100M_WRITE_REQUEST)
			{	TRIPB_100M_WRITE_REQUEST=0;	TRIPB_1KM_WRITE_REQUEST=1;	}
	}

	if(ODO_WRITE_MODE==0)
	{
		if(SEASON_1KM_WRITE_REQUEST)
		{	SEASON_1KM_WRITE_REQUEST=0;	ODO_WRITE_MODE=1;	}
		else
		{
			#if(TRIP_SAVE_ACTIVE)
			{
		 		if(TRIPA_1KM_WRITE_REQUEST)
				{	TRIPA_1KM_WRITE_REQUEST=0;	ODO_WRITE_MODE=4;	}
		 	
			}
			#endif
			
				if(TRIPB_1KM_WRITE_REQUEST)
				{	TRIPB_1KM_WRITE_REQUEST=0;	ODO_WRITE_MODE=5;	}
			
		}
	}

	if(!EEPROM_WRITE_BUSY)
	{
		switch(ODO_WRITE_MODE)
		{
			case 1:	{	ODO_SEASON_WRITE(1);	ODO_WRITE_MODE=2;	break;	}
			case 2:	{	ODO_SEASON_WRITE(2);	ODO_WRITE_MODE=3;	break;	}
			case 3:	{	ODO_SEASON_WRITE(3);	ODO_WRITE_MODE=0;	break;	}
			case 4:	{	ODO_TRIPA_WRITE();		ODO_WRITE_MODE=0;	break;	}
			case 5:	{	ODO_TRIPB_WRITE();		ODO_WRITE_MODE=0;	break;	}
			default:{												ODO_WRITE_MODE=0;	break;	}
		}
		
  }
  
//---------------------------------------------------------
	if(!EEPROM_WRITE_BUSY)
	{
		if(TEL_SERVICE_DAY_WRITE_REQ)
		{
			TEL_SERVICE_DAY_WRITE_REQ=0;
			//TEL_COMM_SERVICE_DAY_SAVE();保养里程移植完成时移除注释
		}
//		if(1==FRS_ODO.write_falg) //风险行驶里程  //FS11-A3  DISABLE
//		{
//  		FRS_ODO_SEASON_WRITE(); 
//  		FRS_ODO.write_falg=0; 
//		}
	}
}

//*****************************************************************************
// FunName: ODO_TRIP_SEASON_CHECK
// Desc.	:	check season and trip,to avoid trip>season
// inputs : none
// outputs: none
//*****************************************************************************
void ODO_TRIP_SEASON_CHECK(void)
{
	if((!SEASON_ERR) && (TRIPA_VALUE>SEASON_VALUE))
	{
		TRIPA_VALUE=(uint16)SEASON_VALUE; ODO_TRIPA_UPDATE_CHECKSUM();
	}

	#if(TRIPB_ACTIVE)
	{
		if((!SEASON_ERR) && (TRIPB_VALUE>SEASON_VALUE))
		{
			TRIPB_VALUE=(uint16)SEASON_VALUE; ODO_TRIPB_UPDATE_CHECKSUM();
		}
	}
	#endif
}

//*****************************************************************************
// FunName: ODO_SEASON_DIAG_SETUP
// Desc.	:	diagnoise modify season
// inputs : none
// outputs: none
//*****************************************************************************
#if(ODO_DIAG_ENABLE)
void ODO_DIAG_TASK(uint32 season)
{
	ODO_SEASON_DIAG_SETUP(season);
}
#endif

//*****************************************************************************
// FunName: SEASON_100ms_UPDATE
// Desc.	:	caculate odo with speed (for speed not pulse mode)
// inputs : none
// outputs: none
//*****************************************************************************
//extern uint32 FUEL_INC_ODO;


 
void ODO_100MS_UPDATE(void)
{
	uint16 temp,spd_input_value;

	//if(SPD_INPUT_VALUE>2400)	spd_input_value=2400;
	//else if(SPD_INPUT_VALUE<20)	spd_input_value=0;
	//else 
	spd_input_value=SPD_INPUT_VALUE;
	if(spd_input_value>2600)	spd_input_value=2600;//总计里程和小计里程累计车速超过260km/h时，按260km/h计算里程增加 
//---------------------------------------------------------
	SEASON_100MS_COUNTER++;
	if(SEASON_100MS_COUNTER>=20)
	{
		SEASON_100MS_COUNTER=0;
		if((SPD_INPUT_MODE==SPD_CAN_MODE))
		{
			temp=spd_input_value;
			temp*=5;
			SEASON_100MS_REMAINDER+=temp%18;

			
			Trip_A_REMA+=temp%18;
			if(Trip_A_REMA>=18)	{	Trip_A_REMA-=18;	Trip_A_100Ms_M++;	}	
			
			Trip_B_REMA+=temp%18;
			if(Trip_B_REMA>=18)	{	Trip_B_REMA-=18;	Trip_B_100Ms_M++;	}	//cm
			
			
			Trip_Single_REMA+=temp%18;
		  	if(Trip_Single_REMA>=18)	{	Trip_Single_REMA-=18;	Trip_Single_100Ms_M++;	}

		  //	Saling_REAM+=temp%18;  //回收里程被取消
		  	//FRS_ODO.Odo_Add_Ream+=temp%18;  //FS11-A3  DISABLE

		  	temp/=18;

		/* 	if(1==Award_Falg.Falg.Sali)
		 	{
  				if(Saling_REAM>=18)	{	Saling_REAM-=18;	Saling_100Ms_M++;	}
  				
  			//	Trip_A_100Ms_M+=temp;
  				
  				if(Saling_100Ms_M>=10000)
  				{
  					Saling_100Ms_M-=10000;
  					Saling_Odo++;
  				}
		 	}  */


//		 	if(1==FRS_ODO.falg)   //FS11-A3  DISABLE
//		 	{
//		  	if(FRS_ODO.Odo_Add_Ream>=18)	{	FRS_ODO.Odo_Add_Ream-=18;	FRS_ODO.Trip_FRS_100Ms_M++;	}
//		  		
//		  	FRS_ODO.Trip_FRS_100Ms_M+=temp;
//		  		
//		 		if(FRS_ODO.Trip_FRS_100Ms_M>=10000)	
//		 		{
//		 			FRS_ODO.Trip_FRS_100Ms_M-=10000;
//		 			FRS_ODO_SEASON_100M();
//		 		}
//		 	}
			
			if(Trip_A_100Ms_M>=10000) 
			{			  
			  Trip_A_100Ms_M-=10000;
			  ODO_TRIPA_INC_100M();			  
			}
			
			if(Trip_B_100Ms_M>=10000) 
			{
			  Trip_B_100Ms_M-=10000;
			 	ODO_TRIPB_INC_100M();			  
			}
			

      if(Trip_Single_100Ms_M>=10000) 
			{			  
			  Trip_Single_100Ms_M-=10000;
			 	TRIP_Single_VALUE_100M();
        
        if(PRINT_UINT_100m_ADD_CNT<9999) {PRINT_UINT_100m_ADD_CNT++;}
        else {PRINT_UINT_100m_ADD_CNT=0;}   //1000km  or start
			}
			
			Trip_Single_100Ms_M+=temp;
			
			Trip_A_100Ms_M+=temp;
			Trip_B_100Ms_M+=temp;
			
			
			SEASON_100MS_M+=temp;
			FUEL_INC_ODO+=temp;

			if(SEASON_100MS_REMAINDER>=18)	{	SEASON_100MS_REMAINDER-=18;	SEASON_100MS_M++;	}
			if(SEASON_100MS_M>=10000)
			{
				SEASON_100MS_M-=10000;
				ODO_SEASON_INC();
				
				#if(TRIPB_ACTIVE)
					ODO_TRIPB_INC_100M();
				#endif
				 
				//保养里程增加  
				//DIC_INFO_ODO_UPDATE();
			}
		}
	}
//---------------------------------------------------------
	SEASON_20MS_COUNTER++;
	if(SEASON_20MS_COUNTER>=4)  //32M
	{
		SEASON_20MS_COUNTER=0;
		temp=spd_input_value;
		temp*=5;
		SEASON_20MS_REMAINDER+=temp%9;
		temp/=9;
		SEASON_20MS_M+=temp;
		if(SEASON_20MS_REMAINDER>=9)	{	SEASON_20MS_REMAINDER-=9;	SEASON_20MS_M++;	}
		if(SEASON_20MS_M>=32000)
		{
			SEASON_20MS_M-=32000; //续航里程增加
			//DIC_INFO_DTE_32M_UPDATE();
			//TEL_COMM_SEATBELT_ODO_INC();
		// DIC_FUEL_RANGE_UPDATE();	DIC模块移植完成时解除注释
		}
	}
}


//*****************************************************************************
// FunName: ODO_CLEAR_CHECK
// Desc.	:	 
// inputs : none
// outputs: none
//*****************************************************************************
void ODO_CLEAR_TASK(void)
{
	uint8 clear_times,temp[2],temp1[2];
	uint16  SEASON_VALUE_SUM;
	uint8 clear_enable;
	
//-------------------------------------------------------------------------------------------------
	if(SPD_INPUT_VALUE>=20)	clear_enable=0;
	else if((!Signal_Get(SIG_CAN_BCM_FrontLeftDoorAjarStatus)) || (!Signal_Get(SIG_CAN_BCM_FrontRightDoorAjarStatus)))	clear_enable=0;
	else if((!Signal_Get(SIG_CAN_BCM_RearLeftDoorAjarStatus)) || (!Signal_Get(SIG_CAN_BCM_RearRightDoorAjarStatus)))		clear_enable=0;
	//else if((BCM_TrunkAjarStatus!=1) || (!BCM_HoodAjarStatus))									clear_enable=0;
	else clear_enable=1;
	if(!clear_enable)
	{
		ODO_CLEAR_REQUEST_BUTTON=0; //上述条件只适合按键 20200513 jinhao
	//	DIC_BUTTON_IGN_PRESS=0;
	}

	if(SPD_INPUT_VALUE>=20)	{	ODO_CLEAR_REQUEST_DIAG=0;	ODO_CLEAR_REQUEST_CAN=0;	}
//-------------------------------------------------------------------------------------------------
	if(ODO_CLEAR_REQUEST_BUTTON ||ODO_CLEAR_REQUEST_DIAG||ODO_CLEAR_REQUEST_CAN)
	{
		EEPROM_READ(ADDRESS_ODO_CLEAR,1,temp);
		EEPROM_READ(ADDRESS_ODO_CLEAR_SUM,2,temp1);
		if((temp1[0]==0xff)&&(temp1[1]==0xff)){ temp1[0] = 0;temp1[1] = 0;}
		SEASON_VALUE_SUM=temp1[1];  SEASON_VALUE_SUM=(SEASON_VALUE_SUM<<8)+temp1[0];
		SEASON_VALUE_SUM += SEASON_VALUE;

		clear_times=temp[0];
		if(clear_times==0xFF)   clear_times=0;

		if(clear_times>=ODO_CLEAR_TIME)  ODO_CLEAR_CLEAROVER3TIME=1;
		else ODO_CLEAR_CLEAROVER3TIME=0;

		if(clear_times<ODO_CLEAR_TIME && !SEASON_ERR && SEASON_VALUE_SUM<5000)
		{
			clear_times++;
			ODO_SEASON_CLEAR();
			//DIC_INFO_AFE_RESET();
			TEL_COMM_SERVICE_WRITE(1);
			TEL_COMM_SERVICE_DAY_WRITE(1);
    //   _FEED_COP();					// Refresh watchdog

			ODO_SAVE_OFFSET(0);
			temp[0]=clear_times;
			ODOClear_times = clear_times;
			EEPROM_WRITE(ADDRESS_ODO_CLEAR,1,temp);
    //   _FEED_COP();						// Refresh watchdog

			temp1[1]=(uint8)((SEASON_VALUE_SUM>>8)&0xFF);
			temp1[0]=(uint8)(SEASON_VALUE_SUM&0xFF);
			EEPROM_WRITE(ADDRESS_ODO_CLEAR_SUM,2,temp1);
		}
		if(SEASON_VALUE_SUM>=5000) ODO_CLEAR_ODOOVER500KM=1;
		else ODO_CLEAR_ODOOVER500KM=0;

		ODO_CLEAR_REQUEST_BUTTON=0;
		ODO_CLEAR_REQUEST_DIAG=0;
		ODO_CLEAR_REQUEST_CAN=0;
	}
}

/************************************************************
* Purpose:   Get_ODOClear_times
* Parameter: None
* Author:
*************************************************************/
uint8 Get_ODOClear_times(void)
{
	return ODOClear_times;
}

uint32 Get_ODO(void)
{
	return SEASON_VALUE;
}



static rti_slot_t s_slot_10ms;
static rti_slot_t s_slot_100ms;

static struct {
    uint8_t init_done;
} s_ctx;

static void prv_run_10ms_jobs(void)
{
    ODO_WRITE_TASK();
}

static void prv_run_100ms_jobs(void)
{
    if (POWER_IGN_ON) {
        ODO_100MS_UPDATE();
    }
}

static void prv_mcu_init(uint8_t cold_boot)
{
    s_slot_10ms  = RTI_OpenSlot(RTI_10MS);
    s_slot_100ms = RTI_OpenSlot(RTI_100MS);
    s_ctx.init_done = 1u;

    ODOMETER_INIT_RESET(cold_boot);
    LOG_I("init (cold_boot=%u)", (unsigned)cold_boot);
}

static void prv_wakeup_init(void)
{
    LOG_I("wakeup_init");
}

static void prv_on_ign_on(void)
{
    ODOMETER_INIT_IGN();
    LOG_I("on_ign_on");
}

static void prv_tick(void)
{
    if (!s_ctx.init_done) {
        return;
    }
    if (RTI_SlotElapsed(&s_slot_10ms))  { prv_run_10ms_jobs(); }
    if (RTI_SlotElapsed(&s_slot_100ms)) { prv_run_100ms_jobs(); }
}

static void prv_standby(void)
{
    ODOMETER_STANDBY();
    LOG_I("standby");
}

const mod_desc_t mod_odometer = {
    .name        = "odometer",
    .mcu_init    = prv_mcu_init,
    .wakeup_init = prv_wakeup_init,
    .on_ign_on   = prv_on_ign_on,
    .tick        = prv_tick,
    .standby     = prv_standby,
};

SCHED_REGISTER(mod_odometer);

