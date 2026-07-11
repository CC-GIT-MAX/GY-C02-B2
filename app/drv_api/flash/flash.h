/**
 * @file    flash.h
 * @brief   FLASH peripheral driver API
 * @brief   FLASH 外设驱动接口
 *
 * Single header that exposes both the module-level bring-up
 * (Flash_Init()) and any future common-use helpers.  This file
 * replaces the older app/drv_api/flash/flash_init.c + (missing .h)
 * layout.
 */
#ifndef C02B2_DRV_API_FLASH_H
#define C02B2_DRV_API_FLASH_H

#include "result.h"

/**
 * @brief   Initialize the FLASH peripheral
 * @brief   初始化 FLASH 外设
 *
 * @details Pulls the FLASH configuration from board/flash_config.c
 *          and applies it through the vendor SDK driver.
 *
 * @return  c02b2_result_t
 * @retval  C02B2_OK  Initialization succeeded
 */
c02b2_result_t Flash_Init(void);

#endif /* C02B2_DRV_API_FLASH_H */
