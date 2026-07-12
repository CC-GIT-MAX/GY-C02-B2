/**
 * @file    bsp_init.h
 * @brief   Board-level (BSP) initialization: clock / pin / DMA / WDG / power
 *
 * Pulled out of main.c. main.c just calls BSP_Init() then DRV_Init().
 */
#ifndef C02B2_BSP_INIT_H
#define C02B2_BSP_INIT_H

#include "result.h"

/**
 * @brief   Bring up the board support package (clocks, pins, DMA, WDG, power)
 * @brief   初始化 BSP（时钟、引脚、DMA、看门狗、电源管理）
 *
 * @details 按以下顺序初始化：
 *          - CLOCK_SYS_Init + UpdateConfiguration
 *          - PINS_DRV_Init
 *          - DMA_DRV_Init
 *          - POWER_SYS_Init
 *          - WDG_DRV_Init
 *          - INT_SYS_ConfigInit
 *
 * @return  c02b2_result_t
 * @retval  C02B2_OK  Initialization succeeded
 * @retval  C02B2_ERR Vendor driver returned an error (propagated)
 */
c02b2_result_t BSP_Init(void);

#endif /* C02B2_BSP_INIT_H */
