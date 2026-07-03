/**
 * @file    bsp_init.h
 * @brief   Board-level (BSP) initialization: clock / pin / DMA / WDG / power
 *
 * Pulled out of main.c. main.c just calls BSP_Init() then DRV_Init().
 */
#ifndef LBX_BSP_INIT_H
#define LBX_BSP_INIT_H

#include "result.h"

lbx_result_t BSP_Init(void);

#endif /* LBX_BSP_INIT_H */
