/*
 * Copyright 2020-2026 Yuntu Microelectronics Co., Ltd.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * 
 * @file utility_print_config.c
 * @brief 
 * 
 */



#include "utility_print_config.h"
#include "linflexd_uart_config.h"

status_t UTILITY_PRINT_Init(void)
{
    status_t status=STATUS_SUCCESS;
    status=LINFlexD_UART_DRV_Init(2, &Printf_uart_config_State,&Printf_uart_config);
    return status;
}

void printf_char(char ch)
{
    LINFlexD_UART_DRV_SendDataPolling(2, (const uint8_t *) &ch, 1);
}


