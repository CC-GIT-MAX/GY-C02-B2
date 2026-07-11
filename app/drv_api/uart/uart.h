/**
 * @file    uart.h
 * @brief   UART peripheral driver API
 * @brief   UART 外设驱动接口
 *
 * Single header that exposes both the module-level bring-up
 * (Uart_Init()) and any future common-use helpers.  This file
 * replaces the older app/drv_api/uart/uart_init.c + (missing .h)
 * layout.
 */
#ifndef C02B2_DRV_API_UART_H
#define C02B2_DRV_API_UART_H

#include "result.h"

/**
 * @brief   Initialize the UART peripheral
 * @brief   初始化 UART 外设
 *
 * @details Pulls the UART configuration from board/uart_config.c
 *          and applies it through the vendor SDK driver.
 *
 * @return  c02b2_result_t
 * @retval  C02B2_OK  Initialization succeeded
 */
c02b2_result_t Uart_Init(void);

#endif /* C02B2_DRV_API_UART_H */
