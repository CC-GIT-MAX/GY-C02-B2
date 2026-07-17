/**
 * @file    SPD_OUTPUT.h
 * @brief   车速表输出层 - 步进电机驱动、频率输出
 * @brief   Speedometer Output Layer - Stepper motor drive, frequency output
 */
#ifndef C02B2_SPD_OUTPUT_H
#define C02B2_SPD_OUTPUT_H

#include "types.h"

/* 公共函数 */
void SPD_OUTPUT_INIT_RESET(u8 cold_boot);
void SPD_OUTPUT_INIT_IGN(void);
void SPD_OUTPUT_STANDBY(void);
void SPD_OUTPUT_SET_FREQ(void);
void SPD_MOVE(void);

#endif
