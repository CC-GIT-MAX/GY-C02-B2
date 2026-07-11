/**
 * @file    lptmr.h
 * @brief   LPTMR peripheral driver API
 * @brief   LPTMR 外设驱动接口
 *
 * Single header that exposes both the module-level bring-up
 * (Lptmr_Init()) and any future common-use helpers.  This file
 * replaces the older app/drv_api/lptmr/lptmr_init.c + (missing .h)
 * layout.
 */
#ifndef C02B2_DRV_API_LPTMR_H
#define C02B2_DRV_API_LPTMR_H

#include "result.h"

/**
 * @brief   Initialize the LPTMR peripheral
 * @brief   初始化 LPTMR 外设
 *
 * @details Pulls the LPTMR configuration from board/lptmr_config.c
 *          and applies it through the vendor SDK driver.
 *
 * @return  c02b2_result_t
 * @retval  C02B2_OK  Initialization succeeded
 */
c02b2_result_t Lptmr_Init(void);

#endif /* C02B2_DRV_API_LPTMR_H */
