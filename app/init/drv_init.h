/**
 * @file    drv_init.h
 * @brief   Driver init: UART / ADC / eTMR / I2C / FLEXCAN / FLASH
 */
#ifndef C02B2_DRV_INIT_H
#define C02B2_DRV_INIT_H

#include "result.h"

/**
 * @brief   Initialize peripheral drivers (UART, ADC, eTMR, I2C, FLEXCAN, FLASH)
 * @brief   初始化外设驱动（UART、ADC、eTMR、I2C、FLEXCAN、FLASH）
 *
 * @details 必须在 BSP_Init() 之后调用。实际驱动句柄与配置
 *          从 board/<driver>_config.c 中读取。 *
 * @return  c02b2_result_t
 * @retval  C02B2_OK  All drivers initialized
 */
c02b2_result_t DRV_Init(void);

#endif /* C02B2_DRV_INIT_H */
