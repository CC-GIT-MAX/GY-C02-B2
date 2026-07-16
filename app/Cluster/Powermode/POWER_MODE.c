//*****************************************************************************
//	CONFIDENTIAL
// 	Copyright (C) 2014 SH.ETC. All rights reserved.
// 	Module			: $ POWER_MODE.c $
// 	Description	: Power Mode Task
//								include all task of ign on/off task
// 	Version			: $ Rev: 1.0 $
// 	Last UpDate Time: $Date:: 2021-03-23 16:20:46 $
// 	FOOT NOTE		: TBD
// 	AUTHOR			: SH.ETC.
//*****************************************************************************

#include "POWER_MODE.h"

// #include "YTM_IO.h"
// #include "YTM_DRIVER.h"
// #include "YTM_AD.h"
// #include "METER.h"
#include "MATHS.h"
// #include "DEBUG.h"
// #include "ODOMETER.h"
// #include "COMMUNICATION.h"
// #include "DIC.h"
// #include "DIC_INFO.h"
// #include "TELLTALE.h"
#include "KAM.h"
// #include "CALIBRATE.h"
// #include "DIC.h"
// #include "TEL_WIRE_INPUT.h"
// #include "TEL_LCD_INPUT.h"
// #include "COMM_CAN.h"
// #include "MATHS.h"
// #include "CALIBRATE.h"
// #include "Zd_app.h"
// #include "dp_appl.h"
// #include "NM_API.h"
// #include "ODO_SEASON.h"
// #include "COMM_CALIBRATE.h"
// #include "YTM_DELAY.h"
// #include "printf.h"
#include "io.h"
#include "adc.h"
#include "YTM32B1MD1.h"
#include "log.h"
#include "scheduler.h"
#include "rti.h"
#include "signal.h"
#include "can_rx.h"
uint16  VREFH_AD_QUEUE[16];
uint32 AVERAGE_POWER_VREFH_CALU_VALUE;
uint32 VREFH_AD_AVERAGE;

uint16 IGN_AD;
uint16 IGN_AD_AVERAGE;
uint16 IGN_AD_QUEUE[IGN_AD_SUM];

uint16 BATT_AD;
uint16 BATT_AD_AVERAGE;
uint16 BATT_AD_QUEUE[IGN_AD_SUM];
uint8 POWER_IGN_ON;							// ign state,1 means ign on,0 means ign off
uint8 POWER_BAT_ON;
uint8 POWER_SLEEP_ENABLE;				// a flag of low power mode in ign off state
// when 1,means can go "STOP",else go "WAITE" only
uint8 POWER_IGN_ON_COUNTER;			// a counter of ign on 		 
uint16 POWER_IGN_OFF_COUNTER;			// a counter of ign off
uint8 POWER_GC_ON_COUNTER;			// a counter of ign on 		 
uint16 POWER_GC_CLOSE_COUNTER;			// a counter of gc off to talk
uint8 POWER_BAT_ON_COUNTER;
uint16 POWER_BAT_OFF_COUNTER;
uint8 POWER_GC_COMM_COUNTER;
uint8 POWER_GC_OFF_COUNTER;      // a counter of gc off to calu time

uint16 POWER_IGN_AD;							// AD value of ign wire
uint16 POWER_BAT_AD;
uint16 POWER_BAT_AVERAGE_AD;
uint16 Adc0_Se33_Pmc_Vref_Sample;

uint8 POWER_COM_COUNTER;					// a counter of calibrate communicate,+1 every 0.5s,
// POWER_COM_COUNTER<250 means in calibrate mode
uint8 POWER_IGN_COUNTER;					// a counter of ign on time,+1 every 0.5s.
uint8 POWER_SELF_CHECK_COUNTER;  // a counter of self-check time,+1 every 0.5s.
uint16 POWER_IGN_IS_OFF_COUNTER;     //a counter of calibrate ign off ,+1 every 0.01s,

uint16 POWER_IGN_Counter;     //上电之后的计时  500ms/cycle

uint16 POWER_IGN_Voltage;
uint8 POWER_STATE;
uint8 POWER_STATE_BEFORE;
uint8 raub_BAT_Voltage;
uint8 raub_IGN_Voltage;
uint8 POWER_STATE_KL30_FIRST;

unsigned char raub_CAN_CommCounter;

unsigned char Power_High_Counter;
unsigned char Power_Low_Counter;
unsigned char Power_High_Exceed;
unsigned char Power_Low_Exceed;

uint8 POWER_STATE_COUNT[POWER_OV2 + 1];
uint8 POWER_STATE_COUNT_PERIORD[POWER_OV2 + 1];

//unsigned short int Vbatt;
uint8 POWER_FAIL_FLAG;
uint8 POWER_FAIL_COUNT;
uint8 POWER_BAT_COUNTER;
uint8 PowerFault;
uint8 LOW_POWER_FLAG,HIGH_POWER_FLAG;//0-Normal,1_h/lVoltage
uint16 LOW_POWER_BL20S_COUNTER;

uint16 POWER_VALUE[10];
uint16 AVERAGE_POWER_VALUE;
uint8 GC_POWER_STATUS;
uint8 GC_POWER_STATUS_OLD;
uint8 LOW_COUNTER;
uint8 POWER_STATUS_CHANGE_COUNTER;

uint8 GC_WakeUp_MODE;  //0-休眠唤醒，1-30唤醒，2-低电压恢复正常，3-高电压恢复正常

uint8 SOC_Reset_Count;

uint8  POWER_FISRT_ON;

uint8  MCU_30_RESET_E2_FLAG;
uint8  MCU_30_RESET_E2_CNT;

uint8  POWER_GC_CLOSE_3s_Flag; //1-3s后关闭GC

uint8 C02_B2_PowerMode=C02_B2_D3;
uint8 PEPS_PowerMode_Final=0x00;//经过处理的PowerMode值,用于判断KL15, ACC

//define
uint16 GC_COMM_FIRST_TEXT;
uint8  CAN_WAKEUP_GC;
#define LENGTH1  29
uint8 MENU_RETURN[LENGTH1]; 
uint8 Reset_Gc_Falg;
uint8 Reset_Gc_Counter;
uint8 COMM_STANDBY_FLAG=0;		// communication module allow enter STOP mode flag 1_sleep,0_wake up
uint8 DIC_STANDBY_FLAG=0;	// DIC module allow enter STOP mode flag 
uint8  GC_Close_Falg;
uint16 FUEL_IGN_ON_COUNTER;
uint8 METER_STANDBY_FLAG=0;			// meter module allow enter STOP mode flag  IGN ON =0  IGN OFF =1
#define AD_CHANNEL_IGN            1 //15电  CH0
#define AD_CHANNEL_BAT            27 //30电  CH0
uint8  Normal_Rx_Status;
uint8  Normal_Tx_Status;
uint8  Normal_Diag_Status;

uint8  NM_Rx_Status;
uint8  NM_Tx_Status;
T_FLAG8   NM_Tx_State;
#define NM_Tx_State_Power  NM_Tx_State.bi.bit0  //  Power_High_Exceed  Power_Low_Exceed  is Stop send 

#define AD_IGN_OFF_VALUE          90
#define AD_IGN_ON_VALUE           880   
#define AD_IGN_OFF_TIME           500   
#define AD_IGN_ON_TIME            4
uint8 COMM_CAN_SLEEP_FLAG=0u;			// CAN sleep flag
void YTM_DRIVER_INIT_PLL(void)
{

} 
#define DEBUG_WAIT_ONLY 1u			    // if set 1,standby mode will wait only(no stop mode)
uint8 dp_appl_CommunicationControl_get(void)
{
   
   return 0 ; 
}
uint8 Get_CAN_Sleep_Flag(void)
{
  return 0;
}
//define
//*****************************************************************************
// FunName: POWER_MODE_INIT_RESET
// Desc.	:	do initialization when MCU reset
// inputs :	none 
// outputs: none
//*****************************************************************************
void POWER_MODE_INIT_RESET(void)
{
  uint8 cold_boot;
  uint8 i;
  // uint16 PowerVal;
   
  cold_boot=!KAM_CHECK();
  
  // YTM_DRIVER_INIT_RESET(1);
  
  // CALIBRATE_INIT_RESET(cold_boot);
  // COMMUNICATION_INIT_RESET(cold_boot);
  // DIC_INIT_RESET(cold_boot);
  // METER_INIT_RESET(cold_boot);
  // TELLTALE_INIT_RESET(cold_boot);
  // ODOMETER_INIT_RESET(cold_boot);    

    //-------------------------------------------------------------------
    POWER_BAT_AD = 0;
    POWER_BAT_ON = 1u;
    POWER_BAT_ON_COUNTER = 0;
    POWER_BAT_OFF_COUNTER = 0;
  
  POWER_SLEEP_ENABLE=0;
  POWER_IGN_ON_COUNTER=0;
  POWER_IGN_IS_OFF_COUNTER=0;
  POWER_IGN_OFF_COUNTER=0;
  POWER_GC_ON_COUNTER=0;
  POWER_GC_CLOSE_COUNTER=0;
  POWER_GC_COMM_COUNTER=0;
  POWER_GC_OFF_COUNTER=0;
  
  LOW_COUNTER=0;

  POWER_IGN_AD=0;
  Adc0_Se33_Pmc_Vref_Sample=0;
  POWER_IGN_ON=0u;
  POWER_IGN_ON=1u;   
  GC_POWER_STATUS=0;
  GC_POWER_STATUS_OLD=0;
  
  POWER_FISRT_ON=0;
  LOW_POWER_FLAG=1;	 //初始化为电压异常
  HIGH_POWER_FLAG=1;
  LOW_POWER_BL20S_COUNTER=500;
  POWER_STATUS_CHANGE_COUNTER=0;
  
  POWER_BAT_AD=YTM_AD_READ(AD_CHANNEL_BAT);

  for(i=0;i<10;i++) POWER_VALUE[i]=POWER_BAT_AD;
  POWER_BAT_AVERAGE_AD=FILTER_2B_AVERAGE(POWER_VALUE,10);  

  PMC_VREF_SAMPLE();
  
  AVERAGE_POWER_VREFH_CALU_VALUE=1200*POWER_BAT_AVERAGE_AD;
  AVERAGE_POWER_VREFH_CALU_VALUE/=VREFH_AD_AVERAGE;
  AVERAGE_POWER_VALUE=FindBatteryLevel(AVERAGE_POWER_VREFH_CALU_VALUE);
  
  if(AVERAGE_POWER_VALUE>190) {AVERAGE_POWER_VALUE=190; POWER_STATE_BEFORE=POWER_OV2;}
  else if(AVERAGE_POWER_VALUE<80)  {AVERAGE_POWER_VALUE=63; POWER_STATE_BEFORE=POWER_UV2;}
  else if(AVERAGE_POWER_VALUE<90)   {AVERAGE_POWER_VALUE=85;  POWER_STATE_BEFORE=POWER_UV1;}
  else  {POWER_STATE_BEFORE=POWER_ENOK;}
  
  POWER_STATE=POWER_STATE_BEFORE;
  
  GC_WakeUp_MODE=1;
  
  GC_Close_Falg=0;
  SOC_Reset_Count=0;

  POWER_GC_CLOSE_3s_Flag=0;
  
  
  //if(cold_boot){ODO_SEASON_READ();POWER_MODE_INIT_IGN();}
}

//*****************************************************************************
// FunName: POWER_MODE_INIT_IGN
// Desc.	:	do initialization when ign on
// inputs : none
// outputs: none
//*****************************************************************************
void POWER_MODE_INIT_IGN(void)
{
  //YTM_DRIVER_INIT_IGN();
  //YTM_DELAY_xms(50);
  PORT_GC_PW_EN_O;
  PORT_GC_PW_EN_H;
  
  // CALIBRATE_INIT_IGN();
  //  COMM_CAL_INIT_IGN();
  // COMMUNICATION_INIT_IGN();   //add C02
	// METER_AD_TASK();			// Call ad for bitton ign on check
  // DIC_INIT_IGN();
  // METER_INIT_IGN();
  // //ODOMETER_INIT_IGN();
  // TELLTALE_INIT_IGN();
  // vNM_CAN_POWER_INIT_IGN();
  GC_COMM_FIRST_TEXT=0;
  
  POWER_COM_COUNTER=250;
  POWER_GC_COMM_COUNTER=0;
  POWER_GC_OFF_COUNTER=0; //cc add 24/10/30 
  //POWER_MODE_INIT_IGN_15();
}

//*****************************************************************************
// FunName: POWER_MODE_STANDBY
// Desc.	:	do perpares before standby when ign off
// inputs : none
// outputs: none
//*****************************************************************************
void POWER_MODE_STANDBY(void) 
{
  // YTM_DRIVER_STANDBY();
  // CALIBRATE_STANDBY();
  // COMMUNICATION_STANDBY();
  MENU_RETURN[16]=0;
  // //METER_STANDBY();
  // //METER_STANDBY1();
  // //ODOMETER_STANDBY();
  // TELLTALE_STANDBY();

  // DIC_STANDBY();
  POWER_GC_OFF_COUNTER=0;
  POWER_GC_COMM_COUNTER=0;    //下电清零，确保不会继续打印，否则会MCU复位 cc add 24/10/30 
}


void POWER_MODE_INIT_IGN_15(void) //15 上电
{

  // ODOMETER_INIT_IGN();
  
  // DIC_INIT_IGN_15();

  //新增
  
  POWER_IGN_COUNTER=0;
  POWER_SELF_CHECK_COUNTER=0;
  POWER_IGN_Counter=0;
  POWER_IGN_IS_OFF_COUNTER=0;
  CAN_WAKEUP_GC=0;
   
    // METER_INIT_IGN_15();
  	// COMMUNICATION_INIT_IGN();
}

void POWER_MODE_STANDBY_15(void) //15 下电
{
// 	METER_STANDBY();
// 	ODOMETER_STANDBY();
// 	COMMUNICATION_STANDBY();
//   TELLTALE_STANDBY_15();
// //	INIT_CD4051();
	
  POWER_IGN_COUNTER=0;
  POWER_SELF_CHECK_COUNTER=0;
  POWER_IGN_Counter=0;
  POWER_IGN_IS_OFF_COUNTER=0;
  KAM_STANDBY();
}
//*****************************************************************************
// FunName: GC_POWER_STATUS_CHECK
// Desc.	:	check the power status of gc
// inputs : none
// outputs: none
//*****************************************************************************
// extern uint8 ILLU_LCD_NOW_VALUE;

// extern uint8 Reset_Gc_Falg;
// extern uint8 Reset_Gc_Counter;
// extern uint16 FUEL_IGN_ON_COUNTER;

void GC_POWER_STATUS_CHECK(void)  
{
//  static uint8 GC_OPEN_UART=0;

  if(1==Reset_Gc_Falg) 
  {
  	   PORT_GC_PW_EN_O;	// total power(not only gc)
       PORT_GC_PW_EN_L;    //在gc休眠的时候不能在此处断电，所以放在此处
       
       Reset_Gc_Counter++;
       
       if(Reset_Gc_Counter>250) Reset_Gc_Falg=0;
       
       return ; 
  }
	
//	if(0==GC_OPEN_UART&&ILLU_LCD_NOW_VALUE>0) 
//	{
//	  COMM_CAL_INIT_IGN();
//	}
//	
//	GC_OPEN_UART=ILLU_LCD_NOW_VALUE;
	

	if(GC_POWER_STATUS==1) 
	{
		POWER_GC_ON_COUNTER=0;
		
	 	if((!POWER_IGN_ON&&1==COMM_STANDBY_FLAG&&1==DIC_STANDBY_FLAG)||1==GC_Close_Falg || (POWER_UV2==POWER_STATE || POWER_OV2==POWER_STATE))//待机模式
	 	{
	 	  GC_Close_Falg=1;
      POWER_GC_CLOSE_3s_Flag=1;
	 	}

    
    if(POWER_GC_CLOSE_COUNTER>=300 || (POWER_UV2==POWER_STATE || POWER_OV2==POWER_STATE)) //下电时间3s
    {
        GC_POWER_STATUS=0; 
        POWER_MODE_STANDBY();
        POWER_GC_CLOSE_3s_Flag=0;
    }

    
	}
	else
	{
    if(POWER_UV2==POWER_STATE || POWER_OV2==POWER_STATE)
    {
      PORT_GC_PW_EN_O;
      PORT_GC_PW_EN_L;
      if(POWER_UV2==POWER_STATE)  GC_WakeUp_MODE=2;
      if(POWER_OV2==POWER_STATE)  GC_WakeUp_MODE=3;
      return; 
    }
    
		POWER_GC_CLOSE_COUNTER=0;
		
		
		if((POWER_IGN_ON||((0==COMM_STANDBY_FLAG)&&!POWER_IGN_ON))&&0==POWER_GC_CLOSE_3s_Flag)
		{
		  if(POWER_GC_ON_COUNTER<10)  POWER_GC_ON_COUNTER++;
      if(POWER_GC_ON_COUNTER>=4)//&&Close_counter>=20) 
      {
        GC_POWER_STATUS=1; 
        GC_Close_Falg=0;
        FUEL_IGN_ON_COUNTER=0;
        if(1!=GC_WakeUp_MODE) 
        {POWER_MODE_INIT_IGN();}
      }
		}
		else POWER_GC_ON_COUNTER=0;	
	}
}
//*****************************************************************************
// FunName: POWER_STANDBY_TASK
// Desc.	:	check all module allow enter STOP mode
//					if enable,enter STOP mode,else enter WAITE mode
// inputs : none
// outputs: none
//*****************************************************************************
void POWER_STANDBY_TASK(void)
{  
  if(!METER_STANDBY_FLAG)     POWER_SLEEP_ENABLE=0;
  else if(!DIC_STANDBY_FLAG)  POWER_SLEEP_ENABLE=0;
  //else if(!TEL_STANDBY_FLAG)  POWER_SLEEP_ENABLE=0;
  else if(!COMM_STANDBY_FLAG) POWER_SLEEP_ENABLE=0;   
  else POWER_SLEEP_ENABLE=1;
  
  POWER_MODE_SLEEP();
}

//*****************************************************************************
// FunName: IGN_Voltage_CHECK
// Desc.	:
//
// inputs : none
// outputs: none
//*****************************************************************************



void IGN_Voltage_CHECK(void)
{
    uint16 lub_ad;
    uint32 lub_temp;

    IGN_INPUT_FILTER();
    lub_ad = IGN_AD_AVERAGE;
    lub_temp = lub_ad;
    lub_temp *= 11;   //Resolution: 0.25V
    lub_temp /= 20;
    if(lub_temp > 255) lub_temp = 255;
    raub_IGN_Voltage = (uint8) lub_temp;
    
    //raub_IGN_Voltage=120;
}
//*****************************************************************************
// FunName: Bat_Voltage_CHECK
// Desc.	:
//
// inputs : none
// outputs: none
//*****************************************************************************

void Bat_Voltage_CHECK(void)
{
    uint16 lub_ad;
    uint32 lub_temp;

    BATT_INPUT_FILTER();
    lub_ad = BATT_AD_AVERAGE;
    lub_temp = lub_ad;
    lub_temp *= 8 ;  //Resolution: 0.25V  
    lub_temp =lub_temp+175;
    lub_temp /= 26;
    lub_temp=lub_temp-3;
    if(lub_temp > 255) lub_temp = 255;
    //raub_BAT_Voltage = (uint8) lub_temp;
    //vDiag_CAL_DATA_HANDLE(4,raub_BAT_Voltage );
    //raub_BAT_Voltage=120;
}

//*****************************************************************************
// FunName: IGN_INPUT_FILTER
// Desc.	:
//
// inputs : none
// outputs: none
//*****************************************************************************
void IGN_INPUT_FILTER(void)
{
    IGN_AD = YTM_AD_READ(AD_CHANNEL_IGN);
    FILTER_2B_REFRESH(IGN_AD_QUEUE, IGN_AD, IGN_AD_SUM);
    IGN_AD_AVERAGE = FILTER_2B_AVERAGE(IGN_AD_QUEUE, IGN_AD_SUM);
}

//*****************************************************************************
// FunName: BATT_INPUT_FILTER
// Desc.	:
//
// inputs : none
// outputs: none
//*****************************************************************************

void BATT_INPUT_FILTER(void)
{
    BATT_AD = YTM_AD_READ(AD_CHANNEL_BAT);
    FILTER_2B_REFRESH(BATT_AD_QUEUE, BATT_AD, IGN_AD_SUM);
    BATT_AD_AVERAGE = FILTER_2B_AVERAGE(BATT_AD_QUEUE, IGN_AD_SUM);
}

//*****************************************************************************
// FunName: vNM_CAN_INIT_IGN
// Desc.	:	do initialization when MCU reset
// inputs :	none
// outputs: none
//*****************************************************************************
void vNM_CAN_POWER_INIT_IGN(void)
{
    Power_High_Exceed = 0;
    Power_Low_Exceed = 0;
    Power_Low_Counter = 0;
}

void CANPowerCheck(void)
{

#if 1
	static uint8 Power_recover_Counter=0 ;
	static uint8 Power_error_Counter=0 ;

//< 6.6 || >18.3
         raub_BAT_Voltage=AVERAGE_POWER_VALUE;
	if(raub_BAT_Voltage<=POWER_ATD_LOW_STOP  && POWER_IGN_ON ) {	  //batt < 6.6	ig on

		Normal_Rx_Status=1;
		Normal_Tx_Status=1;
		NM_Rx_Status=1;
		NM_Tx_Status=1;
		NM_Tx_State_Power=1;
	} 
	else if(raub_BAT_Voltage<=POWER_ATD_LOW_STOP || raub_BAT_Voltage>=POWER_ATD_HIGH_STOP)
	{
		Power_error_Counter++;
		if(Power_error_Counter>=25)
		{
			Power_error_Counter=25;
			if(raub_BAT_Voltage<=POWER_ATD_LOW_STOP) Power_Low_Exceed=1;
			else if(raub_BAT_Voltage>=POWER_ATD_HIGH_STOP) Power_High_Exceed=1;

			Normal_Rx_Status=0;
			Normal_Tx_Status=0;
			NM_Rx_Status=0;
			NM_Tx_State_Power=0;
		}
		Power_recover_Counter=0;
	}
// >6.9 && < 18.1
	else if(raub_BAT_Voltage>=POWER_ATD_LOW_RESUME && raub_BAT_Voltage<=POWER_ATD_HIGH_RESUME &&!dp_appl_CommunicationControl_get()) 
	{
		Power_error_Counter=0;
		Power_recover_Counter++;
		//if(Power_recover_Counter>=0)
			//if(Power_recover_Counter>=25)
		{
			Power_recover_Counter=25;
			Power_Low_Exceed=0;
			Power_High_Exceed=0;
			Normal_Rx_Status=1;
			Normal_Tx_Status=1;
			NM_Rx_Status=1;
			NM_Tx_State_Power=1;

		}
	}
#else

  if(POWER_STATE != POWER_UV2) POWER_STATE_KL30_FIRST=1;

	if((POWER_STATE == POWER_UV2 && POWER_STATE_KL30_FIRST ==1 )||(POWER_STATE == POWER_OV2))
	{
		 Normal_Rx_Status=0;
		 Normal_Tx_Status=0;
		 NM_Rx_Status=0;
		 NM_Tx_Status=0;
		 NM_Tx_State_Power=0;

	}
// >6.9 && < 18.1  POWER_OV1
	else if(((POWER_STATE == POWER_UV1)||(POWER_STATE == POWER_ENOK)||(POWER_STATE == POWER_OV1)) &&(!dp_appl_CommunicationControl_get())) 
	{
	  if((NM_Tx_State_Power==0) || (NM_WAKE_UP_ON) )
	  {
	     COMM_CAN_Normal_Tx(1);
       COMM_CAN_NM_Tx(1); 
	     COMM_CAN_Normal_Rx(1) ;
       COMM_CAN_NM_Rx(1);
       NM_Tx_State_Power=1;
	  }   
	 
	/*
	  //if(!Get_CAN_Sleep_Flag())
	  {
		Power_Low_Exceed=0;
		Power_High_Exceed=0;
		Normal_Rx_Status=1;
		Normal_Tx_Status=1;
		NM_Rx_Status=1;
	   }
	   
	   NM_Tx_State_Power=1;
	   */
	}
#endif	
}

/**
 * 通过AD读取值15电电压结合需求中提到的时间来决策当前电源状态
 */
void POWER_IGN_MODE_CHECK(void)
{

  // 低风险项：ADC读取失败后未发送特定的值以避免出现后续问题

  // 读取电压值，如果读取失败会初始化ADC
  POWER_IGN_AD=YTM_AD_READ(AD_CHANNEL_IGN);
  //POWER_IGN_AD=YTM_AD_READ(AD_CHANNEL_IGN);
  
  //POWER_IGN_AD=AD_IGN_ON_VALUE+20;
  if(POWER_IGN_ON)							// check ignoff when ign on
  {
    POWER_IGN_ON_COUNTER=0;
      
    //if(POWER_IGN_AD<AD_IGN_OFF_VALUE) 
    if(0==IGN_STATE)
    {
      POWER_IGN_OFF_COUNTER++;
      if(POWER_IGN_OFF_COUNTER>=AD_IGN_OFF_TIME)
      {
        POWER_IGN_ON=0;					// clear ign on flag
        POWER_MODE_STANDBY_15(); 	// do all prepare for ign off
      }
    }
    else POWER_IGN_OFF_COUNTER=0;
  }
  else 													// check ignon when ign off
  {
    POWER_IGN_OFF_COUNTER=0;
      
    //if(POWER_IGN_AD>=AD_IGN_ON_VALUE) 
    if(1==IGN_STATE)
    {
      POWER_IGN_ON_COUNTER++;
      if(POWER_IGN_ON_COUNTER>=AD_IGN_ON_TIME) 
      {
        POWER_IGN_ON=1;					// set ign on flag
        COMM_CAN_SLEEP_FLAG=0;
        POWER_MODE_INIT_IGN_15();	// do all initialize for ign on
        //ABNORMAL_POWEROFF=1;
        //ABNORMAL_POWEROFF_WRITE();
      }
    } 
    else POWER_IGN_ON_COUNTER=0;
  }
}

void PMC_VREF_SAMPLE(void)  //内部参考电压读取
{
  Adc0_Se33_Pmc_Vref_Sample=YTM_AD_READ(33);
  FILTER_2B_REFRESH(VREFH_AD_QUEUE, Adc0_Se33_Pmc_Vref_Sample, 16);
  VREFH_AD_AVERAGE = FILTER_2B_AVERAGE(VREFH_AD_QUEUE, 16);
  
}


const uint16 POWER_BAT_AD_IN[30] ={0,  0,  938,1022,1100,1260,1406,1474,1512,1574,1624,1672,1713,1752,1785,1820,1845,1872,1895,1918,1940,1977,2012,2028,2048,2072,2095,65535,65535,65535};
const uint16 POWER_BAT_AD_OUT[30]={45, 55, 60, 65,  70,  80,  90,  95,  100, 105, 110, 115, 120, 125, 130, 135, 140, 145, 150, 155, 160, 170, 180, 185, 190, 200, 210, 210,  210,  210};

uint16 FindBatteryLevel(uint16 in_value) 
{
	uint8 i,match;
	uint32 temp;
	uint16 in_1,in_2,out;

	if(in_value>=POWER_BAT_AD_IN[26])	out=210;
	else if(in_value<=POWER_BAT_AD_IN[0]) {  out=POWER_BAT_AD_OUT[0];  }
	else
	{
		for(i=0,match=0;(i<(30-2)) && (!match);i++)
		{
			in_1=POWER_BAT_AD_IN[i];  in_2=POWER_BAT_AD_IN[i+1];
			if(in_2>POWER_BAT_AD_IN[26])	{	match=1;	out=POWER_BAT_AD_OUT[i];	}
			else
			{
				if((in_value>=in_1) && (in_value<in_2))
				{
					match=1;
					temp=in_value-in_1;
					temp*=POWER_BAT_AD_OUT[i+1]-POWER_BAT_AD_OUT[i];
					temp/=in_2-in_1;
					temp+=POWER_BAT_AD_OUT[i]+1;
					out=(uint16)temp;
				}
			}
		}
	}

	return out;
}


//*****************************************************************************
// FunName: 
// Desc.	:
// periord: 50 MS
// inputs : none
// outputs: none
//*****************************************************************************
uint8 POWER_LOW_counter;
uint8  POWER_BAT_test;
uint16  POWER_BAT_testvalue;
void POWER_MANAGEMENT_CHACK(void)  //优化:+滤波+延时
{
  // uint32 PowerVal=0;
  uint8 temp;
  
  PMC_VREF_SAMPLE();
  
  Bat_Voltage_CHECK();
  POWER_BAT_AD=YTM_AD_READ(AD_CHANNEL_BAT);
  
  if(GC_POWER_STATUS==0) {LOW_POWER_BL20S_COUNTER=500;}
  
  FILTER_2B_REFRESH(POWER_VALUE,(uint16)POWER_BAT_AD,10);
  POWER_BAT_AVERAGE_AD=FILTER_2B_AVERAGE(POWER_VALUE,10);
  
  AVERAGE_POWER_VREFH_CALU_VALUE=1200*POWER_BAT_AVERAGE_AD;
  AVERAGE_POWER_VREFH_CALU_VALUE/=VREFH_AD_AVERAGE;
  
  AVERAGE_POWER_VALUE=FindBatteryLevel(AVERAGE_POWER_VREFH_CALU_VALUE);
  
  if(AVERAGE_POWER_VALUE>190) {AVERAGE_POWER_VALUE=190;}
  else if(AVERAGE_POWER_VALUE<60)  {AVERAGE_POWER_VALUE=60;}
  
  if(LOW_POWER_BL20S_COUNTER<500)//2s 计时期间
  {   
  	LOW_POWER_BL20S_COUNTER++; 
  	if(AVERAGE_POWER_VALUE>=80) //约等于上电时的7V
  	{
  		LOW_POWER_FLAG=0;
    //	HIGH_POWER_FLAG=1;
    	LOW_POWER_BL20S_COUNTER=500;
    LOW_COUNTER=0;		
  	}
  }
  else
  {
    if(AVERAGE_POWER_VALUE<65) //6.5 
	  {
	    if(!LOW_POWER_FLAG)  //停止低压
	    {
	      LOW_COUNTER++;
	       if(LOW_COUNTER>=100){
        		LOW_POWER_FLAG=1;
        		HIGH_POWER_FLAG=0;	 
        		LOW_POWER_BL20S_COUNTER=0;
        		POWER_STATE_BEFORE=POWER_UV2;
	       }
	    }
	  } 
	  else
	  {
	  	if(AVERAGE_POWER_VALUE>=65&&AVERAGE_POWER_VALUE<70)//滞缓 7 920
	  	{
	  	  HIGH_POWER_FLAG=0;
        if(POWER_STATE_BEFORE<=POWER_UV2)    POWER_STATE_BEFORE=POWER_UV2;
        if(POWER_STATE_BEFORE>=POWER_UV1)    POWER_STATE_BEFORE=POWER_UV1;
	  	} 
	  	else if(AVERAGE_POWER_VALUE>=70&&AVERAGE_POWER_VALUE<90)//背光减半  低压  9 1125 
	  	{
	  	  LOW_COUNTER=0;
	  		LOW_POWER_FLAG=0;
	  		HIGH_POWER_FLAG=0; 
	  		 
	  		if(POWER_LOW_counter<20) POWER_LOW_counter++;
	  		if(POWER_LOW_counter>10) POWER_STATE_BEFORE=POWER_UV1;
	  	}
	  	else if(AVERAGE_POWER_VALUE>90&&AVERAGE_POWER_VALUE<=95)//滞缓  9.5 1205 
	  	{
	  	  LOW_COUNTER=0;  	HIGH_POWER_FLAG=0;   LOW_POWER_FLAG=0; 
        if(POWER_STATE_BEFORE<=POWER_UV1)    POWER_STATE_BEFORE=POWER_UV1;
        if(POWER_STATE_BEFORE>=POWER_ENOK)   POWER_STATE_BEFORE=POWER_ENOK;
	  	}
	  	else if(AVERAGE_POWER_VALUE>95&&AVERAGE_POWER_VALUE<=155)//正常工作电压	  15.5  1578
	  	{ 
	  	  LOW_COUNTER=0;
	  		LOW_POWER_FLAG=0;   POWER_STATE_BEFORE=POWER_ENOK;
	  		HIGH_POWER_FLAG=0;  POWER_LOW_counter=0;
	  	} 
	  	else if(AVERAGE_POWER_VALUE>155&&AVERAGE_POWER_VALUE<=160)//滞缓 	 16  1593
	  	{ 
	  	  LOW_COUNTER=0;   	HIGH_POWER_FLAG=0; 	LOW_POWER_FLAG=0;
        if(POWER_STATE_BEFORE<=POWER_ENOK)    POWER_STATE_BEFORE=POWER_ENOK;
        if(POWER_STATE_BEFORE>=POWER_OV1)     POWER_STATE_BEFORE=POWER_OV1;
	  	} 
	  	else if(AVERAGE_POWER_VALUE>160&&AVERAGE_POWER_VALUE<=180)//高压 	 
	  	{ 
	  	  LOW_COUNTER=0; 	HIGH_POWER_FLAG=0; POWER_STATE_BEFORE=POWER_OV1; 	LOW_POWER_FLAG=0;
	  	} 
	  	else if(AVERAGE_POWER_VALUE>180&&AVERAGE_POWER_VALUE<185)  //滞缓   18  1657 
      { 
        LOW_COUNTER=0;	LOW_POWER_FLAG=0; 
        if(POWER_STATE_BEFORE<=POWER_OV1)     POWER_STATE_BEFORE=POWER_OV1;
        if(POWER_STATE_BEFORE>=POWER_OV2)     POWER_STATE_BEFORE=POWER_OV2;
      } 
	  	else if(AVERAGE_POWER_VALUE>185) //停止高压 18.5 1665 
	  	{ 
	  	  LOW_COUNTER=0;
		  	LOW_POWER_FLAG=0;
	    	HIGH_POWER_FLAG=1;  POWER_STATE_BEFORE=POWER_OV2;
	  	}
	  }
  }
  if(POWER_STATE!=POWER_STATE_BEFORE)
  {
    if(POWER_STATE_BEFORE>POWER_STATE)  {temp=POWER_STATE_BEFORE-POWER_STATE;}
    else if(POWER_STATE>POWER_STATE_BEFORE) {temp=POWER_STATE-POWER_STATE_BEFORE;}
    else {temp=0;}  
    
    if(POWER_GC_COMM_COUNTER>30)
    {
      LOG_I("POWER STATE= %d",POWER_STATE);
      LOG_I("POWER STATE BEFORE= %d",POWER_STATE_BEFORE);
    }
    
    if(POWER_STATUS_CHANGE_COUNTER<200&&1==temp) POWER_STATUS_CHANGE_COUNTER++;
    else POWER_STATE=POWER_STATE_BEFORE;
  }
  else {POWER_STATUS_CHANGE_COUNTER=0;}

  GC_POWER_STATUS_OLD=GC_POWER_STATUS;//睡眠唤醒,电压在6~7V
  
  if(LOW_POWER_FLAG==1) GC_WakeUp_MODE=2;
  if(HIGH_POWER_FLAG==1)GC_WakeUp_MODE=3;
}  

//*****************************************************************************
// FunName: C02_B2_PowerMode_Update
// Desc: 更新电源模式
//*****************************************************************************
void C02_B2_PowerMode_Update(void)
{
  uint8 mode=0;
  uint8 last_mode=0x00;
  uint8 is_timeout=0;
  uint8 PEPS_PowerModeValidity=0x00;
  uint8 PEPS_PowerMode=0x00;
  
  static uint8 last_c02_b2_power_mode=C02_B2_D3;
  static uint8 d2_to_d1_count=0;

  is_timeout=CanRx_IsMsgTimedOut(0x2fc);
  PEPS_PowerModeValidity=Signal_Get(SIG_CAN_PEPS_PowerModeValidity);
  PEPS_PowerMode=Signal_Get(SIG_CAN_PEPS_PowerMode);

   if(1==is_timeout)
   {//超时处理
    last_mode=Signal_GetStored(SIG_CAN_PEPS_PowerMode);
    if(0x00==last_mode||0x01==last_mode)
    {
      mode=0x00;
    }
    else if(0x02==last_mode)
    {
      mode=0x02;
    }
    else
    {
      mode=last_mode;
    }
   }
   else if(0x02!=PEPS_PowerModeValidity)
   {//信号无效处理
    if(0x00==PEPS_PowerMode||0x01==PEPS_PowerMode)
    {
      mode=0x00;
    }
    else if(0x02==PEPS_PowerMode)
    {
      mode=0x02;
    }
    else
    {
      mode=PEPS_PowerMode;
    }

   }
   else
   {//正常情况
      mode=PEPS_PowerMode;
   }
   PEPS_PowerMode_Final=mode;//更新经过处理的PowerMode值
   if (1==IGN_STATE&&0==Get_CAN_Sleep_Flag())
   {
    if(C02_B2_PowerMode==C02_B2_D1)
    {//已经是这个状态直接退出
      return;
    }
    if(C02_B2_D2_1==last_c02_b2_power_mode||C02_B2_D2_2==last_c02_b2_power_mode)
    {
      d2_to_d1_count++;
      if(d2_to_d1_count>20)//d2跳d1延时200ms
      {
        C02_B2_PowerMode=C02_B2_D1;
        last_c02_b2_power_mode=C02_B2_D1;
        d2_to_d1_count=0;
      }
    }
    else
    {
      C02_B2_PowerMode=C02_B2_D1;
      last_c02_b2_power_mode=C02_B2_D1;
    }
   }
   else if(0==IGN_STATE&&1==ACC_STATE&&0==Get_CAN_Sleep_Flag())
   {
    if(C02_B2_PowerMode==C02_B2_D2_1)
    {//已经是这个状态直接退出
      return;
    }
    d2_to_d1_count=0;
    C02_B2_PowerMode=C02_B2_D2_1;
    last_c02_b2_power_mode=C02_B2_D2_1;
   }
   else if (0==IGN_STATE&&0==ACC_STATE&&0==Get_CAN_Sleep_Flag())
   {
    if(C02_B2_PowerMode==C02_B2_D2_2)
    {//已经是这个状态直接退出
      return;
    }
    d2_to_d1_count=0;
    C02_B2_PowerMode=C02_B2_D2_2;
    last_c02_b2_power_mode=C02_B2_D2_2;
   }
   else if (0==IGN_STATE&&0==ACC_STATE&&1==Get_CAN_Sleep_Flag())
   {//已经是这个状态直接退出
    if(C02_B2_PowerMode==C02_B2_D3)
    {
      return;
    }
    d2_to_d1_count=0;
    C02_B2_PowerMode=C02_B2_D3;
    last_c02_b2_power_mode=C02_B2_D3;
   }
   else
   {//超出判断范围保持之前的状态
    d2_to_d1_count=0;
   }   
   
   
}



//*****************************************************************************
// FunName: POWER_MODE_SLEEP
// Desc.	:	enter low power mode (STOP or WAITE)
// inputs : none
// outputs: none
//*****************************************************************************
void POWER_MODE_SLEEP(void) 
{
  uint8 dummy=0;
  
  #if(DEBUG_WAIT_ONLY)
  if(POWER_SLEEP_ENABLE) 
  {
    SCB->SCR &= ~(SCB_SCR_SLEEPDEEP_Msk);   // Sleep mode
    __asm("wfi\n");
  }
  #else 
  {
    if(POWER_SLEEP_ENABLE) 
    {
//      IPC->CTRL[98]=1;              // WKU clock enbale
//      WKU->MER=4;                   // WKU select lpTimer0_IRQ
//      
//      SCU->FIRC_CTRL=0;             // Disable FIRC 
//      SCU->SIRC_CTRL=0x0F;          // Enable SIRC in all low power mode
//      SCU->CLKS=3;                  // Select SIRC as system clock source
//      while((SCU->STS&0x03)!=3) {}
//      
//      SCU->PLL_CTRL=0;              // Disable PLL
//      SCU->FXOSC_CTRL=0x00001810;   // Disable Fxose
//      SCU->DIV=0;
//      while(SCU->DIVSTS!=0) {}
//      EFM->CTRL=0x00060000;         // Flashdiv/1
      
      //SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;    // Deepsleep mode 2.5mA
      //PCU->CTRL &= ~(PCU_CTRL_RPMEN_MASK | PCU_CTRL_STANDBYEN_MASK);
            
      SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;    // Standby mode 1mA
      PCU->CTRL |= PCU_CTRL_RPMEN_MASK | PCU_CTRL_STANDBYEN_MASK;
            
      //SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;    // Powerdown mode 0.5mA
      //PCU->CTRL &= ~(PCU_CTRL_RPMEN_MASK | PCU_CTRL_STANDBYEN_MASK);
      //PCU->CTRL |= PCU_CTRL_RPMEN_MASK;
      
      SCU->CMU_CTRL = 0;            // Disable clock monitor
      SysTick->CTRL = 0x00;         // Disable system tic
      
      SCB->SCR &= ~(SCB_SCR_SLEEPONEXIT_Msk);     

      asm("wfi\n");
      POWER_FISRT_ON=10;
    } 
    else
    {
      SCB->SCR &= ~(SCB_SCR_SLEEPONEXIT_Msk);
      if( (SCU->STS&SCU_STS_PLL_LOCK_MASK) && ((SCU->STS&SCU_STS_CLKST_MASK)==1) ){}
      else YTM_DRIVER_INIT_PLL();
      SysTick->CTRL  = 0x00000007;  // Enable system tic
      dummy=0;
      dummy++;
    }
  }
  #endif
}

void WKU_IRQHandler(void)
{
}
static rti_slot_t s_my_100ms;
/* ========================================================================
 * Scheduler integration (mod_desc_t five-piece set)
 * 电源模块调度器集成：按五件套结构接入 scheduler 框架
 * ======================================================================*/

/* Module-private RTI period slots (opened once in mcu_init).
 * 模块私有 RTI 周期槽位（mcu_init 中一次性打开）。
 * 对应原 RTOS.c 的 10ms / 100ms / 500ms 三层调度粒度。*/
static rti_slot_t s_slot_10ms;
static rti_slot_t s_slot_100ms;
static rti_slot_t s_slot_500ms;

#define MOD_NAME  "PWR"

/* Private context ------------------------------------------------------- */
static struct {
    uint8_t init_done;
} s_ctx;

/* --- 10 ms sub-task (原 RTOS.c YTM_RTI_10MS_FLAG 对应内容) ------------- */
static void prv_run_10ms_jobs(void)
{
    /* 更新电源模式*/
    C02_B2_PowerMode_Update();

    /* 电源管理核心：电压采样+滤波+分级(POWER_MANAGEMENT_CHACK 原 10ms) */
    POWER_MANAGEMENT_CHACK();

    /* IGN 状态机 (KL15/KL30 边沿去抖+INIT_IGN_15/STANDBY_15 回调, 原 10ms) */
    POWER_IGN_MODE_CHECK();

    /* GC 上下电状态机 (POWER_GC_CLOSE_COUNTER>=300 对应 3s, 原 10ms) */
    GC_POWER_STATUS_CHECK();

    /* 休眠条件聚合 (检查各模块 STANDBY_FLAG → POWER_SLEEP_ENABLE) */
    POWER_STANDBY_TASK();

    /* 原 RTOS.c L131-L140: IGN OFF 累计计时 (每 10ms +1，上限 30000 = 300s) */
    if (!POWER_IGN_ON) {
        if (POWER_IGN_IS_OFF_COUNTER < 30000u) {
            POWER_IGN_IS_OFF_COUNTER++;
        }
    } else {
        POWER_IGN_IS_OFF_COUNTER = 0u;
    }
}

/* --- 100 ms sub-task (原 RTOS.c YTM_RTI_100MS_FLAG 对应内容) ----------- */
static void prv_run_100ms_jobs(void)
{
    /* 原 RTOS.c L202-L212: GC 上电/断电时分别维护 GC_COMM / GC_OFF 计数器
     * (每 100ms +1，上限 250 = 25s) */
    if (1u == GC_POWER_STATUS) {
        POWER_GC_OFF_COUNTER = 0u;
        if (POWER_GC_COMM_COUNTER < 250u) {
            POWER_GC_COMM_COUNTER++;
        }
    } else {
        POWER_GC_COMM_COUNTER = 0u;
        if (POWER_GC_OFF_COUNTER < 250u) {
            POWER_GC_OFF_COUNTER++;
        }
    }
}

/* --- 500 ms sub-task (原 RTOS.c YTM_RTI_500MS_FLAG 对应内容) ----------- */
static void prv_run_500ms_jobs(void)
{
    /* 原 RTOS.c L293-L299: IGN ON 期间维护四个 0.5s 周期计数器
     * 上限 250 对应 250 * 500ms = 125s；POWER_IGN_Counter 上限 50000 = 6.94h */
    if((POWER_UV2==POWER_STATE || POWER_OV2==POWER_STATE))
    {

    }
    else
    {
      if (POWER_IGN_ON) 
      {
          if (POWER_COM_COUNTER         < 250u)    POWER_COM_COUNTER++;
          if (POWER_IGN_COUNTER         < 250u)    POWER_IGN_COUNTER++;
          if (POWER_SELF_CHECK_COUNTER  < 250u)    POWER_SELF_CHECK_COUNTER++;
          if (POWER_IGN_Counter         < 50000u)  POWER_IGN_Counter++;
      }      
    }

}

/* mod_desc_t hooks ------------------------------------------------------ */

/**
 * @brief   mod_desc_t mcu_init 钩子：复位级初始化+打开 RTI 槽位
 * @brief   对应原 POWER_MODE_INIT_RESET，参数 cold_boot 仅用于日志；
 *          原函数内部通过 !KAM_CHECK() 自行判断冷/热启动，保持不变。
 *
 * @param[in]  cold_boot  1 = 上电冷启动，0 = 其他复位源
 */
static void prv_mcu_init(uint8_t cold_boot)
{
    s_slot_10ms  = RTI_OpenSlot(RTI_10MS);
    s_slot_100ms = RTI_OpenSlot(RTI_100MS);
    s_slot_500ms = RTI_OpenSlot(RTI_500MS);
    s_ctx.init_done = 1u;

    POWER_MODE_INIT_RESET();
    LOG_I("init (cold_boot=%u)", (unsigned)cold_boot);
}

/**
 * @brief   mod_desc_t wakeup_init 钩子：深睡/STOP 唤醒后恢复
 * @brief   目前电源模块无独立 NVIC/唤醒源要恢复，留作占位。
 */
static void prv_wakeup_init(void)
{
    LOG_I("wakeup_init");
}

/**
 * @brief   mod_desc_t on_ign_on 钩子：IGN 上升沿初始化
 * @brief   对应原 POWER_MODE_INIT_IGN (GC 上电、计数器复位等)。
 *          后续接入真实 KL15 检测后，由 mod_power 在 tick 中监测
 *          SIG_IGN_ON 上升沿并调用 Scheduler_OnIgnOn() 触发本钩子。
 */
static void prv_on_ign_on(void)
{
    POWER_MODE_INIT_IGN();
    LOG_I("on_ign_on");
}

/**
 * @brief   mod_desc_t tick 钩子：按 RTI 子周期分发三个粒度任务
 * @brief   分发顺序 = 快(10ms) → 中(100ms) → 慢(500ms)，
 *          保证 IGN 去抖、电压分级等时间敏感工作优先执行。
 */
static void prv_tick(void)
{
    if (!s_ctx.init_done) {
        return;
    }
    if (RTI_SlotElapsed(&s_slot_10ms))  { prv_run_10ms_jobs(); }
    if (RTI_SlotElapsed(&s_slot_100ms)) { prv_run_100ms_jobs(); }
    if (RTI_SlotElapsed(&s_slot_500ms)) { prv_run_500ms_jobs(); }
}

/**
 * @brief   mod_desc_t standby 钩子：进入 STOP/STANDBY 前的清理
 * @brief   对应原 POWER_MODE_STANDBY (GC_OFF/COMM 计数器清零等)。
 *          main.c 检测 SIG_SLEEP_READY==1 时调用 Scheduler_Standby()
 *          触发本钩子。
 */
static void prv_standby(void)
{
    POWER_MODE_STANDBY();
    LOG_I("standby");
}

/* Module descriptor (registered in scheduler.c::g_sched_modules[]) ------- */

const mod_desc_t mod_power_mode = {
    .name        = "power_mode",
    .mcu_init    = prv_mcu_init,
    .wakeup_init = prv_wakeup_init,
    .on_ign_on   = prv_on_ign_on,
    .tick        = prv_tick,
    .standby     = prv_standby,
};

/* Linker-level guard: force the symbol to be retained even if no C code
 * references mod_power_mode (scheduler.c uses extern + g_sched_modules[]
 * entry as the "real source", this macro is the double-check). */
SCHED_REGISTER(mod_power_mode);

