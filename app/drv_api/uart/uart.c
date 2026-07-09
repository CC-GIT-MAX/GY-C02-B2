/**
 * @file    uart.c
 * @brief   UART peripheral driver implementation
 * @brief   UART 外设驱动实现
 *
 * Module-level bring-up + (future) common-use helpers live here.
 * Replaces the older app/drv_api/uart/uart_init.c.
 */
#include "uart.h"
#include "sdk_project_config.h"

#define LOG_NAME  "UAR"
#include "log.h"

c02b2_result_t Uart_Init(void)
{
    LINFlexD_UART_DRV_Init(0, &COMM_uart_config_State, &COMM_uart_config);
    return C02B2_OK;
}
