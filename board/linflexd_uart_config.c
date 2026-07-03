/*
 * Copyright 2020-2026 Yuntu Microelectronics Co., Ltd.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * 
 * @file linflexd_uart_config.c
 * @brief 
 * 
 */



#include "linflexd_uart_config.h"


/*COMM_uart_config*/
linflexd_uart_state_t COMM_uart_config_State;
const linflexd_uart_user_config_t COMM_uart_config = {
    .baudRate=250000U,
    .parityCheck=false,
    .parityType=LINFlexD_UART_PARITY_EVEN,
    .stopBitsCount=LINFlexD_UART_ONE_STOP_BIT,
    .wordLength=LINFlexD_UART_8_BITS,
    .txTransferType=LINFlexD_UART_USING_DMA,
    .rxTransferType=LINFlexD_UART_USING_DMA,
    .txDMAChannel=1,
    .rxDMAChannel=0,
};

