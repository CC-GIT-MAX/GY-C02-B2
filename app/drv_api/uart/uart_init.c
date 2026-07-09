/**
 * @file    uart_init.c
 * @brief   Initialize LINFlexD UART instance 0 (debug printf + external comm)
 */
#include "sdk_project_config.h"

/**
 * @brief   Initialize LINFlexD UART instance 0
 *
 * @details Used for two purposes:
 *          - debug printf via the LOG_* macros (UTILITY_PRINT_Init()
 *            is called earlier in DRV_Init() to wire the putchar hook);
 *          - external serial comm with the factory test box.
 */
void Uart_Init(void)
{
    LINFlexD_UART_DRV_Init(0, &COMM_uart_config_State, &COMM_uart_config);
}
