/**
 * @file    drv_init.c
 * @brief   Driver init
 */
#include "drv_init.h"

#include "sdk_project_config.h"

#define LOG_NAME  "DRV"
#include "log.h"

lbx_result_t DRV_Init(void)
{
    UTILITY_PRINT_Init();
    lpTMR_DRV_Init(0, &LPTMR_Config, false);
    ADC_DRV_ConfigConverter(0, &adc_config0);
    eTMR_DRV_Init(0, &ETMR_CM_Config0, &ETMR_CM_Config0_State);
    eTMR_DRV_InitPwm(3, &ETMR3_PWM_Config0);
    I2C_DRV_MasterInit(0, &I2C_MasterConfig0, &I2C_MasterConfig0_State);
    LINFlexD_UART_DRV_Init(0, &COMM_uart_config_State, &COMM_uart_config);
    FLEXCAN_DRV_Init(1, &private_can_State, &private_can);
    FLEXCAN_DRV_Init(2, &public_can_State,  &public_can);
    FLASH_DRV_Init(0, &flash_config0, &flash_config0_State);
    LOG_I("DRV init OK");
    return LBX_OK;
}
