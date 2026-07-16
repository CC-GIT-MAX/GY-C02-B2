#include "types.h"

#ifndef __ODO_SEASON_H_
#define __ODO_SEASON_H_

#define OdometerbackupEnable  0x00//0x01:enable,0x00:disable
//-------------------------------------------------------------------
// public functions
//-------------------------------------------------------------------

void ODO_SEASON_INIT_RESET(uint8 cold_boot);

void ODO_SEASON_INIT_IGN(void);

void ODO_SEASON_STANDBY(void);

void ODO_SEASON_PULSE_INC(void);

void ODO_SEASON_INC(void);

void ODO_SEASON_CLEAR(void);

uint8 ODO_SEASON_RAM_CHECK(void);

void ODO_SEASON_READ(void);

void ODO_SEASON_WRITE(uint8 index);

uint8 ODO_SEASON_GET_CHECKSUM(void);

void ODO_SEASON_DIAG_SETUP(uint32 season);

void ODO_SEASON_BACKUP_TASK(void);

uint8 Get_IPK_OdometerbackupEnable(void);

void ODO_SAVE_OFFSET(uint32 tar);
uint32 ODO_GET_TOTAL(void);
uint32 ODO_GET_TOTAL_offset(void);
//-------------------------------------------------------------------
// public macros
//-------------------------------------------------------------------

//-------------------------------------------------------------------
// public vars
//-------------------------------------------------------------------

extern uint32 SEASON_VALUE;
extern uint8 SEASON_ERR;
extern uint8 SEASON_1KM_WRITE_REQUEST;
extern uint8 SEASON_100M_WRITE_REQUEST;

extern uint32 SEASON_BAK1;
extern uint32 SEASON_BAK2;
extern uint8 SEASON_CHECKSUM;
extern uint32 SEASON_VALUE_offset;

//-------------------------------------------------------------------
#endif