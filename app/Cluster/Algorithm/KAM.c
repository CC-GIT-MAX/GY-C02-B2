//*****************************************************************************
//	CONFIDENTIAL
// 	Copyright (C) 2014 SH.ETC. All rights reserved.
// 	Module			: $ KAM.c $
// 	Description	: Keep Alive Memory Module
// 	Version			: $ Rev: 1.0 $
// 	Last UpDate Time: $Date:: 2021-03-23 16:20:46 $
// 	FOOT NOTE		: TBD
// 	AUTHOR			: SH.ETC.
//*****************************************************************************

#include "KAM.h"

// #include "SPD_CALCULATE.h"
// #include "FUEL_CALCULATE.h"
// #include "SPD_INPUT.h"
// #include "TAC_INPUT.h"

#define SPD_INPUT_VALUE   1//宏定义，后续从SPD_INPUT.h中获取
#define TAC_SEND_VALUE    1//宏定义，后续从TAC_INPUT.h中获取
#define FUEL_NOW_STEPS    1//宏定义，后续从FUEL_CALCULATE.h中获取

uint16 KAM_55AA=0x55AA;					// RAM constant flag1
uint16 KAM_CHECKSUM=3;			// meters now value(XXX_NOW_STEPS) checksum 
uint16 KAM_AA55=0XAA55;				 	// RAM constant flag2

//*****************************************************************************
// FunName: KAM_INIT_RESET
// Desc.	:	do initialization when MCU reset
// inputs :	cold_boot : 1 cold boot,0 warm boot 
// outputs: none
//*****************************************************************************
void KAM_INIT_RESET(uint8 cold_boot) 
{
  if(cold_boot) { }
}

//*****************************************************************************
// FunName: KAM_INIT_IGN
// Desc.	:	do initialization when ign on
// inputs : none
// outputs: none
//*****************************************************************************
void KAM_INIT_IGN(void) 
{
}

//*****************************************************************************
// FunName: KAM_STANDBY
// Desc.	:	do perpares before standby when ign off
// inputs : none
// outputs: none
//*****************************************************************************
void KAM_STANDBY(void) 
{
  KAM_55AA=0x55AA;
  KAM_AA55=0xAA55;
}

//*****************************************************************************
// FunName: KAM_CHECK
// Desc.	:	check all KAM flags
// inputs : none
// outputs: 0 means KAM damage,cold boot needed,else run warm boot
//*****************************************************************************
uint8 KAM_CHECK(void) 
{
  uint8 kam;
  uint16 kam_checksum;
  
  if(KAM_55AA!=0x55AA)          kam=0u;
  else if(KAM_AA55!=0xAA55)     kam=0u;
  else if(SPD_INPUT_VALUE>3800)   kam=0u;
  else if(TAC_SEND_VALUE>3800)   kam=0u;
  else if(FUEL_NOW_STEPS>3800)  kam=0u;
 // else if(TEMP_NOW_STEPS>3800)  kam=0u;
  else 
  {
    kam_checksum=SPD_INPUT_VALUE+TAC_SEND_VALUE+FUEL_NOW_STEPS+1;
    if(kam_checksum!=KAM_CHECKSUM)  kam=0u;
    else kam=1u;
  }
  
  return kam;
}

//*****************************************************************************
// FunName: KAM_UPDATE_CHECKSUM
// Desc.	:	update KAM_CHECKSUM
// inputs : none
// outputs: none
//*****************************************************************************
void KAM_UPDATE_CHECKSUM(void)
{
  KAM_CHECKSUM=SPD_INPUT_VALUE+TAC_SEND_VALUE+FUEL_NOW_STEPS+1;
}