/**
 * @file    flash.c
 * @brief   FLASH peripheral driver implementation
 * @brief   FLASH 外设驱动实现
 *
 * Module-level bring-up + (future) common-use helpers live here.
 * Replaces the older app/drv_api/flash/flash_init.c.
 */
#include "flash.h"
#include "sdk_project_config.h"

#define LOG_NAME  "FLA"
#include "log.h"

c02b2_result_t Flash_Init(void)
{
    FLASH_DRV_Init(0, &flash_config0, &flash_config0_State);
    return C02B2_OK;
}
