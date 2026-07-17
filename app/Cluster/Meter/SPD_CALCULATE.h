/**
 * @file    SPD_CALCULATE.h
 * @brief   车速表计算层 - 步进电机步数计算
 * @brief   Speedometer Calculation Layer - Stepper motor step calculation
 */
#ifndef C02B2_SPD_CALC_H
#define C02B2_SPD_CALC_H

#include "types.h"

/* 公共函数 */
void SPD_CALC_INIT_RESET(u8 cold_boot);
void SPD_CALC_INIT_IGN(void);
void SPD_CALC_STANDBY(void);
void SPD_CALCULATE(void);
void SPD_CALC_SET_MOVE_SPD(void);
u16 SPD_CALC_LINEAR(u16 in_value);

/* 公共变量 */
extern u16 SPD_NEW_STEPS;       /**< 新计算的目标步数 */
extern u16 SPD_TAR_STEPS;       /**< 经滤波后的目标步数 */
extern u16 SPD_NOW_STEPS;       /**< 当前步进电机步数 */

#endif
