/**
 * @file    drv_init.h
 * @brief   Driver init: UART / ADC / eTMR / I2C / FLEXCAN / FLASH
 */
#ifndef LBX_DRV_INIT_H
#define LBX_DRV_INIT_H

#include "result.h"

/**
 * @brief   Initialize peripheral drivers (UART, ADC, eTMR, I2C, FLEXCAN, FLASH)
 * @brief   初始化外设驱动（UART、ADC、eTMR、I2C、FLEXCAN、FLASH）
 *
 * @details Must be called after BSP_Init(). The actual driver
 *          handles and configurations are picked up from
 *          board/<driver>_config.c.
 *
 * @return  lbx_result_t
 * @retval  LBX_OK  All drivers initialized
 */
lbx_result_t DRV_Init(void);

#endif /* LBX_DRV_INIT_H */
