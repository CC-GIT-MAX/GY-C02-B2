/**
 * @file    drv_init.c
 * @brief   Driver init
 */
#include "drv_init.h"

#include "sdk_project_config.h"

#define LOG_NAME  "DRV"
#include "log.h"

/**
 * @brief   Initialize peripheral drivers
 * @brief   初始化外设驱动
 *
 * @details Each driver pulls its config from board/<driver>_config.c.
 *          The order is loosely "no dependencies" - any inter-driver
 *          ordering constraints should be added here.
 *
 * @return  lbx_result_t  Always LBX_OK (errors are vendor-internal and logged)
 */
lbx_result_t DRV_Init(void)
{
    /* Print utility: needed for LOG_* macros to work. */
    UTILITY_PRINT_Init();
    /* LPTMR: 1 kHz tick source for RTI (RTI_Init() comes later in main). */
    lpTMR_DRV_Init(0, &LPTMR_Config, false);
    /* ADC: configure the converter; specific channels read on demand. */
    ADC_DRV_ConfigConverter(0, &adc_config0);
    /* eTMR channel 0: counter mode (for general timing). */
    eTMR_DRV_Init(0, &ETMR_CM_Config0, &ETMR_CM_Config0_State);
    /* eTMR channel 3: PWM mode (drives stepper motors via board). */
    eTMR_DRV_InitPwm(3, &ETMR3_PWM_Config0);
    /* I2C: master mode (touch controller, IO expander, etc.). */
    I2C_DRV_MasterInit(0, &I2C_MasterConfig0, &I2C_MasterConfig0_State);
    /* LINFlexD UART: used for debug printf and external comm. */
    LINFlexD_UART_DRV_Init(0, &COMM_uart_config_State, &COMM_uart_config);
    /* FlexCAN instance 1: private bus (body / chassis domain). */
    FLEXCAN_DRV_Init(1, &private_can_State, &private_can);
    /* FlexCAN instance 2: public bus (powertrain / infotainment). */
    FLEXCAN_DRV_Init(2, &public_can_State,  &public_can);
    /* Flash driver: needed by app/storage/kv. */
    FLASH_DRV_Init(0, &flash_config0, &flash_config0_State);
    LOG_I("DRV init OK");
    return LBX_OK;
}
