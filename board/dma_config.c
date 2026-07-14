/*
 * Copyright 2020-2026 Yuntu Microelectronics Co., Ltd.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * 
 * @file dma_config.c
 * @brief 
 * 
 */


#include <stddef.h>
#include "dma_config.h"


/*dma_config0*/
extern void DMA_UART0_RX_FUNCTION_CALLBACK(void *parameter, dma_chn_status_t status);

const dma_channel_config_t dma_config0 = {
    .virtChnConfig=0,
    .source=DMA_REQ_LINFlexD0_RX,
    .callback=DMA_UART0_RX_FUNCTION_CALLBACK,
    .callbackParam=NULL,
};
/*dma_config1*/
extern void DMA_UART0_TX_FUNCTION_CALLBACK(void *parameter, dma_chn_status_t status);

const dma_channel_config_t dma_config1 = {
    .virtChnConfig=1,
    .source=DMA_REQ_LINFlexD0_TX,
    .callback=DMA_UART0_TX_FUNCTION_CALLBACK,
    .callbackParam=NULL,
};
/*dma_config2*/
extern void DMA_UART2_RX_FUNCTION_CALLBACK(void *parameter, dma_chn_status_t status);

const dma_channel_config_t dma_config2 = {
    .virtChnConfig=2,
    .source=DMA_REQ_LINFlexD2_RX,
    .callback=DMA_UART2_RX_FUNCTION_CALLBACK,
    .callbackParam=NULL,
};
/*dma_config3*/
extern void DMA_UART2_TX_FUNCTION_CALLBACK(void *parameter, dma_chn_status_t status);

const dma_channel_config_t dma_config3 = {
    .virtChnConfig=3,
    .source=DMA_REQ_LINFlexD2_TX,
    .callback=DMA_UART2_TX_FUNCTION_CALLBACK,
    .callbackParam=NULL,
};


const dma_channel_config_t *const dmaChnConfigArray[NUM_OF_CONFIGURED_DMA_CHANNEL] = {
    &dma_config0,
    &dma_config1,
    &dma_config2,
    &dma_config3,
};

const dma_user_config_t dmaController_InitConfig = {
    .haltOnError = false,
    .maxChannelForChLink=true,
};

dma_chn_state_t dma_config0_State;
dma_chn_state_t dma_config1_State;
dma_chn_state_t dma_config2_State;
dma_chn_state_t dma_config3_State;



dma_chn_state_t *const dmaChnState[NUM_OF_CONFIGURED_DMA_CHANNEL]={
    &dma_config0_State,
    &dma_config1_State,
    &dma_config2_State,
    &dma_config3_State,
};

dma_state_t dmaState;


