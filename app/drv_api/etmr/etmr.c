/**
 * @file    etmr.c
 * @brief   eTMR peripheral driver implementation
 * @brief   eTMR 外设驱动实现
 *
 * Module-level bring-up + (future) common-use helpers live here.
 * Replaces the older app/drv_api/etmr/etmr_init.c.
 */
#include "etmr.h"
#include "sdk_project_config.h"

#define LOG_NAME  "ETM"
#include "log.h"

c02b2_result_t Etmr_Init(void)
{
    eTMR_DRV_Init(0, &ETMR_CM_Config0, &ETMR_CM_Config0_State);
    eTMR_DRV_InitPwm(3, &ETMR3_PWM_Config0);
    return C02B2_OK;
}
