/**
 * @file    lptmr.c
 * @brief   LPTMR peripheral driver implementation
 * @brief   LPTMR 外设驱动实现
 *
 * Module-level bring-up + (future) common-use helpers live here.
 * Replaces the older app/drv_api/lptmr/lptmr_init.c.
 */
#include "lptmr.h"
#include "sdk_project_config.h"

#define LOG_NAME  "LPT"
#include "log.h"

c02b2_result_t Lptmr_Init(void)
{
    lpTMR_DRV_Init(0, &LPTMR_Config, false);
    return C02B2_OK;
}
