#include "types.h"

#ifndef __ODOMETER_H_
#define __ODOMETER_H_

//-------------------------------------------------------------------
// public functions
//-------------------------------------------------------------------

void ODOMETER_INIT_RESET(uint8 cold_boot);

void ODOMETER_INIT_IGN(void);

void ODOMETER_STANDBY(void);

void ODO_PULSE_INC(void);

void ODO_WRITE_TASK(void);

void ODO_TRIP_SEASON_CHECK(void);

void ODO_DIAG_TASK(uint32 season);

void ODO_100MS_UPDATE(void);
void ODO_CLEAR_TASK(void);
uint8 Get_ODOClear_times(void);
uint32 Get_ODO(void);
void ODOMETER_TRIP_DIVEND_CLEAR(void);
//-------------------------------------------------------------------
// public macros
//-------------------------------------------------------------------

#define TRIP_SAVE_ACTIVE  0u		// set 1 when trip need store into eeprom
#define TRIPB_ACTIVE			0u		// set 1 when tripb need

#define ODO_DIAG_ENABLE		1u
//-------------------------------------------------------------------
// public vars
//-------------------------------------------------------------------

extern T_FLAG8   ODO_CLEAR_REQUEST; 
#define ODO_CLEAR_REQUEST_BUTTON	ODO_CLEAR_REQUEST.bi.bit0
#define ODO_CLEAR_REQUEST_DIAG		ODO_CLEAR_REQUEST.bi.bit1
#define ODO_CLEAR_REQUEST_CAN			ODO_CLEAR_REQUEST.bi.bit2
#define ODO_CLEAR_ODOOVER500KM		ODO_CLEAR_REQUEST.bi.bit3
#define ODO_CLEAR_CLEAROVER3TIME	ODO_CLEAR_REQUEST.bi.bit4

#define ODO_CLEAR_TIME 3
extern uint8 ODOClear_times;

extern uint16 ODO_100M_PULSES_NUMBER_INT;
extern uint16 ODO_100M_PULSES_NUMBER_REM;

extern uint8 ODO_WRITE_MODE;

extern uint16  Trip_A_REMA,Trip_A_100Ms_M;
extern uint16  Trip_B_REMA,Trip_B_100Ms_M;

//define
#define	ADDRESS_OFFSET_ODO						0x80  //4
#define	ADDRESS_TRIPA						0x08    //4
#define	ADDRESS_TRIPB						0x0C    //4
#define	ADDRESS_LANGUAGE_MODE		0xF6
#define	ADDRESS_ODO_CLEAR				0xF8
#define ADDRESS_ODO_CLEAR_SUM				0x1AD    //总计里程清零总和 2
#define SPD_PULSE_MODE            0
#define SPD_CAN_MODE              1
 
void EEPROM_READ(uint16 eep_address,uint8 rd_number,uint8 * p_header);
void EEPROM_WRITE_NO_DELAY(uint16 eep_address,uint8 wr_number,uint8* p_header);
extern uint8  TRIP_CLEAR_TXGC_FLAG; 
extern uint8 IPK_RstTrip2;
extern uint8 IPK_RstTrip2_Send_Cnt;
extern uint8 IPK_RstTrip1;
extern uint8 IPK_RstTrip1_Send_Cnt;  
extern uint32 TRIP_Single_VALUE;


//define

//-------------------------------------------------------------------
#endif