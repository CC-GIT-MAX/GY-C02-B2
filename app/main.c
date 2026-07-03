/* USER CODE BEGIN Header */
/* you can remove the copyright */
/*
 * Copyright 2020-2026 Yuntu Microelectronics Co., Ltd.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * 
 * @file main.c
 * @brief 
 * 
 */

/* USER CODE END Header */
#include "sdk_project_config.h"
/* Includes ------------------------------------------------------------------*/
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */
/* USER CODE END PV */

/* Private function declare --------------------------------------------------*/
/* USER CODE BEGIN PFDC */
void I2C_DRIVER_MASTER_CALLBACK(i2c_master_event_t event, void *userData)
{
  uint8_t *pUserData;
  if(I2C_MASTER_EVENT_TX_END==event)
  {
    pUserData = userData;
  }
  else if(I2C_MASTER_EVENT_RX_END==event)
  {
    pUserData = userData;
  }
  
  (void)pUserData;
}

void DMA_UART0_RX_FUNCTION_CALLBACK(void *parameter, dma_chn_status_t status)
{
}


void DMA_UART0_TX_FUNCTION_CALLBACK(void *parameter, dma_chn_status_t status)
{
}

void DMA_UART2_RX_FUNCTION_CALLBACK(void *parameter, dma_chn_status_t status)
{
}

void DMA_UART2_TX_FUNCTION_CALLBACK(void *parameter, dma_chn_status_t status)
{
}

/* USER CODE END PFDC */
static void Board_Init(void);

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* USER CODE END 0 */


/**
 * @brief  The application entry point.
 * @retval int
 */
int main(void)
{
    /* USER CODE BEGIN 1 */
    /* USER CODE END 1 */ 
    Board_Init();
    /* USER CODE BEGIN 2 */
    /* USER CODE END 2 */

    /* Infinite loop */
    /* USER CODE BEGIN WHILE */
    while (1)
    {
        /* USER CODE END WHILE */
        /* USER CODE BEGIN 3 */
    }
    /* USER CODE END 3 */
}

static void Board_Init(void)
{
    CLOCK_SYS_Init(g_clockManConfigsArr,CLOCK_MANAGER_CONFIG_CNT,g_clockManCallbacksArr,CLOCK_MANAGER_CALLBACK_CNT);
    CLOCK_SYS_UpdateConfiguration(CLOCK_MANAGER_ACTIVE_INDEX,CLOCK_MANAGER_POLICY_AGREEMENT);
    PINS_DRV_Init(NUM_OF_CONFIGURED_PINS0,g_pin_mux_InitConfigArr0);
    DMA_DRV_Init(&dmaState,&dmaController_InitConfig,dmaChnState,dmaChnConfigArray,NUM_OF_CONFIGURED_DMA_CHANNEL);
    UTILITY_PRINT_Init();
    lpTMR_DRV_Init(0,&LPTMR_Config,false);
    ADC_DRV_ConfigConverter(0,&adc_config0);
    eTMR_DRV_Init(0,&ETMR_CM_Config0,&ETMR_CM_Config0_State);
    eTMR_DRV_InitPwm(3,&ETMR3_PWM_Config0);
    I2C_DRV_MasterInit(0,&I2C_MasterConfig0,&I2C_MasterConfig0_State);
    LINFlexD_UART_DRV_Init(0,&COMM_uart_config_State,&COMM_uart_config);
    FLEXCAN_DRV_Init(1,&private_can_State,&private_can);
    FLEXCAN_DRV_Init(2,&public_can_State,&public_can);
    FLASH_DRV_Init(0,&flash_config0,&flash_config0_State);
    WDG_DRV_Init(0,&wdg_config0);
    POWER_SYS_Init(&powerConfigsArr,POWER_MANAGER_CONFIG_CNT,NULL,POWER_MANAGER_CALLBACK_CNT);
    INT_SYS_ConfigInit();
}

/* USER CODE BEGIN 4 */
/* USER CODE END 4 */
