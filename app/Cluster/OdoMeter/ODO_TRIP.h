#include "types.h"

#ifndef __ODO_TRIP_H_
#define __ODO_TRIP_H_

//-------------------------------------------------------------------
// public functions
//-------------------------------------------------------------------

void ODO_TRIP_INIT_RESET(uint8 cold_boot);

void ODO_TRIP_INIT_IGN(void);

void ODO_TRIP_STANDBY(void);

void ODO_TRIP_CAL_READ(void);

void ODO_TRIPA_PULSE_INC(void);
void ODO_TRIPA_INC_100M(void);
void ODO_TRIPA_CLEAR(void);
void ODO_TRIPA_UPDATE_CHECKSUM(void);
void ODO_TRIPA_READ(void);	
void ODO_TRIPA_WRITE(void);	

void ODO_TRIPB_PULSE_INC(void);
void ODO_TRIPB_INC_100M(void);
void ODO_TRIPB_CLEAR(void);
void ODO_TRIPB_UPDATE_CHECKSUM(void);
void ODO_TRIPB_READ(void);	
void ODO_TRIPB_WRITE(void);	
void TRIP_Single_VALUE_100M(void); 

//-------------------------------------------------------------------
// public macros
//-------------------------------------------------------------------

//-------------------------------------------------------------------
// public vars
//-------------------------------------------------------------------

extern uint32 TRIPA_VALUE;
extern uint8 TRIPA_1KM_WRITE_REQUEST;
extern uint8 TRIPA_100M_WRITE_REQUEST;

extern uint32 TRIPB_VALUE;
extern uint8 TRIPB_1KM_WRITE_REQUEST;
extern uint8 TRIPB_100M_WRITE_REQUEST;

//-------------------------------------------------------------------
#endif