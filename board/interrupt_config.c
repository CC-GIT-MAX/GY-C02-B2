/*
 * Copyright 2020-2026 Yuntu Microelectronics Co., Ltd.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * 
 * @file interrupt_config.c
 * @brief 
 * 
 */


#include <stddef.h>
#include "interrupt_config.h"


void INT_SYS_ConfigInit(void)
{
    INT_SYS_DisableIRQGlobal();
    /* WDG0_IRQn(22) WDG0_IRQHandler*/
    INT_SYS_SetPriority(WDG0_IRQn,3);
    INT_SYS_EnableIRQ(WDG0_IRQn);
    /* I2C0_Master_IRQn(24) I2C0_Master_IRQHandler*/
    INT_SYS_SetPriority(I2C0_Master_IRQn,4);
    INT_SYS_EnableIRQ(I2C0_Master_IRQn);
    /* LINFlexD0_IRQn(31) LINFlexD0_IRQHandler*/
    INT_SYS_SetPriority(LINFlexD0_IRQn,2);
    INT_SYS_EnableIRQ(LINFlexD0_IRQn);
    /* LINFlexD2_IRQn(35) LINFlexD2_IRQHandler*/
    INT_SYS_SetPriority(LINFlexD2_IRQn,2);
    INT_SYS_EnableIRQ(LINFlexD2_IRQn);
    /* lpTMR0_IRQn(58) lpTMR0_IRQHandler*/
    INT_SYS_SetPriority(lpTMR0_IRQn,4);
    INT_SYS_EnableIRQ(lpTMR0_IRQn);
    /* GPIOB_IRQn(60) GPIOB_IRQHandler*/
    INT_SYS_SetPriority(GPIOB_IRQn,3);
    INT_SYS_EnableIRQ(GPIOB_IRQn);
    /* CAN1_ORed_IRQn(85) CAN1_ORed_IRQHandler*/
    INT_SYS_SetPriority(CAN1_ORed_IRQn,1);
    INT_SYS_EnableIRQ(CAN1_ORed_IRQn);
    /* CAN1_Error_IRQn(86) CAN1_Error_IRQHandler*/
    INT_SYS_SetPriority(CAN1_Error_IRQn,1);
    INT_SYS_EnableIRQ(CAN1_Error_IRQn);
    /* CAN1_Wake_Up_IRQn(87) CAN1_Wake_Up_IRQHandler*/
    INT_SYS_SetPriority(CAN1_Wake_Up_IRQn,1);
    INT_SYS_EnableIRQ(CAN1_Wake_Up_IRQn);
    /* CAN1_ORed_0_15_MB_IRQn(88) CAN1_ORed_0_15_MB_IRQHandler*/
    INT_SYS_SetPriority(CAN1_ORed_0_15_MB_IRQn,1);
    INT_SYS_EnableIRQ(CAN1_ORed_0_15_MB_IRQn);
    /* CAN1_ORed_16_31_MB_IRQn(89) CAN1_ORed_16_31_MB_IRQHandler*/
    INT_SYS_SetPriority(CAN1_ORed_16_31_MB_IRQn,1);
    INT_SYS_EnableIRQ(CAN1_ORed_16_31_MB_IRQn);
    /* CAN2_ORed_IRQn(92) CAN2_ORed_IRQHandler*/
    INT_SYS_SetPriority(CAN2_ORed_IRQn,1);
    INT_SYS_EnableIRQ(CAN2_ORed_IRQn);
    /* CAN2_Error_IRQn(93) CAN2_Error_IRQHandler*/
    INT_SYS_SetPriority(CAN2_Error_IRQn,1);
    INT_SYS_EnableIRQ(CAN2_Error_IRQn);
    /* CAN2_Wake_Up_IRQn(94) CAN2_Wake_Up_IRQHandler*/
    INT_SYS_SetPriority(CAN2_Wake_Up_IRQn,1);
    INT_SYS_EnableIRQ(CAN2_Wake_Up_IRQn);
    /* CAN2_ORed_0_15_MB_IRQn(95) CAN2_ORed_0_15_MB_IRQHandler*/
    INT_SYS_SetPriority(CAN2_ORed_0_15_MB_IRQn,1);
    INT_SYS_EnableIRQ(CAN2_ORed_0_15_MB_IRQn);
    /* CAN2_ORed_16_31_MB_IRQn(96) CAN2_ORed_16_31_MB_IRQHandler*/
    INT_SYS_SetPriority(CAN2_ORed_16_31_MB_IRQn,1);
    INT_SYS_EnableIRQ(CAN2_ORed_16_31_MB_IRQn);
    INT_SYS_EnableIRQGlobal();
}

