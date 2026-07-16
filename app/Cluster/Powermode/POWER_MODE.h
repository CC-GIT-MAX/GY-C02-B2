#include "types.h"

#ifndef __POWER_MODE_H_
#define __POWER_MODE_H_
#include "scheduler.h"
// #include "METER.h"
// #include "DIC.h"
// #include "TELLTALE.h"
// #include "CALIBRATE.h"

//-------------------------------------------------------------------
// public functions
//-------------------------------------------------------------------

void POWER_MODE_INIT_RESET(void);

void POWER_MODE_INIT_IGN(void);

void POWER_MODE_INIT_IGN_15(void);

void POWER_MODE_STANDBY(void); 

void POWER_MODE_STANDBY_15(void);

void POWER_STANDBY_TASK(void);

void GC_POWER_STATUS_CHECK(void);

void POWER_IGN_MODE_CHECK(void);
void POWER_MODE_CHECK1(void);

void POWER_MODE_SLEEP(void);

void vNM_CAN_POWER_INIT_IGN(void);

//uint8 BAT_Voltage_STATE(void) ;


void IGN_Voltage_CHECK(void);
void IGN_INPUT_FILTER(void);

void Bat_Voltage_CHECK(void);
void BATT_INPUT_FILTER(void);

void CANPowerCheck(void);

void PMC_VREF_SAMPLE(void);
uint16 FindBatteryLevel(uint16 in_value);

void POWER_MANAGEMENT_CHACK(void);

//-------------------------------------------------------------------
// public macros
//-------------------------------------------------------------------\

#define IGN_AD_SUM								8

#define POWER_ATD_HIGH_STOP     205//887   //18.3V 
#define POWER_ATD_HIGH_RESUME   190//867   //18.1v  17.6


#define POWER_DIAG_LOW_STOP      88//9V    
#define POWER_DIAG_LOW_RESUME      101//10V    

#define POWER_DIAG_HIGH_STOP    160//16V

#define POWER_ATD_LOW_STOP      58//7V    
#define POWER_ATD_LOW_RESUME    60//6.9V


#define POWER_nUV2ThresExit   	69 //6.9V
#define POWER_nUV2Thres   		66 //6.6V
#define POWER_nUV1ThresExit   	88//8.7V
#define POWER_nUV1Thres   		85//8.4V
#define POWER_nOV1ThresExit   	166//16.1V
#define POWER_nOV1Thres   		168//16.4V
#define POWER_nOV2ThresExit   	185//18.1V
#define POWER_nOV2Thres   		185//18.1V

#define POWER_nUVYes      5//0.3V
#define POWER_nOVYes      5//0.3V


#define FUEL_CHECK_TEST   1
#define BL_DEBUG_ENABLE 0
//-------------------------------------------------------------------
// public vars
//-------------------------------------------------------------------
extern uint8 raub_BAT_Voltage;
extern uint8 POWER_IGN_ON;
extern uint8 POWER_BAT_ON;

extern uint8 POWER_SLEEP_ENABLE;

extern uint8 POWER_COM_COUNTER;
extern uint8 POWER_IGN_COUNTER;
extern uint8 POWER_SELF_CHECK_COUNTER;
extern uint16 POWER_IGN_Voltage;
extern uint16 POWER_IGN_OFF_COUNTER;			// a counter of ign off
extern uint8 POWER_GC_ON_COUNTER;	
extern uint8 POWER_GC_COMM_COUNTER;
extern uint8 POWER_GC_OFF_COUNTER;

extern uint16 POWER_IGN_Counter;
extern uint16 POWER_IGN_IS_OFF_COUNTER;
extern uint16 IGN_AD;
extern uint16 IGN_AD_AVERAGE;

extern uint16 POWER_IGN_AD;
extern uint16 BATT_AD;
extern uint16 BATT_AD_AVERAGE;
extern uint16 AVERAGE_POWER_VALUE;

extern unsigned char raub_CAN_CommCounter;
extern unsigned char IGNOFF_Door_NW_Off;
extern unsigned char Power_High_Exceed;
extern unsigned char Power_Low_Exceed; 

extern uint8 POWER_FAIL_FLAG;
extern uint8 POWER_FAIL_COUNT;


extern uint8 POWER_STATE;

typedef enum
{

    POWER_UV2 = 0,//<6.5
    POWER_UV1=1,//6.5-9
    POWER_ENOK=2,//9V-16V
    POWER_OV1=3,//16V-18V
    POWER_OV2=4,//>18V
    POWER_Fail = POWER_UV2
} enPower_State;

extern uint8 LOW_POWER_FLAG,HIGH_POWER_FLAG;//0-Normal,1_h/lVoltage
extern uint16 LOW_POWER_BL20S_COUNTER;

extern uint8 GC_POWER_STATUS;

extern uint8 PowerFault;

extern uint8 GC_WakeUp_MODE;

extern uint16 Adc0_Se33_Pmc_Vref_Sample;

extern uint8  POWER_FISRT_ON;
extern uint8  MCU_30_RESET_E2_FLAG;
extern uint8  MCU_30_RESET_E2_CNT;

extern uint8  POWER_GC_CLOSE_3s_Flag;

#define IGN_STATE   (0X04==PEPS_PowerMode_Final||0X02==PEPS_PowerMode_Final)
#define ACC_STATE   (0X01==PEPS_PowerMode_Final)
// 电源模式枚举
typedef enum {
    C02_B2_D1  = 0x01,  // KL15 ON (行驶模式)
    C02_B2_D2_1 = 0x02,  // ACC ON (附件模式)
    C02_B2_D2_2 = 0x03,  // OFF/Standby (待机模式)
    C02_B2_D3   = 0x4   // Sleep (休眠模式)
} PowerMode_t;
extern uint8 C02_B2_PowerMode;
//-------------------------------------------------------------------
//#define  IC_Ready_Sleep_State ((METER_STANDBY_FLAG==1)&&(TEL_STANDBY_FLAG==1)&&(CAL_EOL_WriteEnalbe==1)&&(Get_Diagnose_state()==0u))?1u:0u
extern const mod_desc_t mod_power_mode;
#endif