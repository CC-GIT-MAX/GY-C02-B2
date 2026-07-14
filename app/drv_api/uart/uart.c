/**
 * @file    uart.c
 * @brief   UART peripheral driver implementation
 * @brief   UART 外设驱动实现
 *
 * 模块级初始化以及（未来可能新增的）公共辅助函数均在本文件实现。
 * 取代了较早的 app/drv_api/uart/uart_init.c。
 */
#include "uart.h"
#include "sdk_project_config.h"

#define MOD_NAME  "UAR"
#include "log.h"

c02b2_result_t Uart_Init(void)
{
    LINFlexD_UART_DRV_Init(0, &COMM_uart_config_State, &COMM_uart_config);
    return C02B2_OK;
}
