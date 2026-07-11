/**
 * @file    etmr.h
 * @brief   eTMR peripheral driver API
 * @brief   eTMR 外设驱动接口
 *
 * Single header that exposes both the module-level bring-up
 * (Etmr_Init()) and any future common-use helpers.  This file
 * replaces the older app/drv_api/etmr/etmr_init.c + (missing .h)
 * layout.
 */
#ifndef C02B2_DRV_API_ETMR_H
#define C02B2_DRV_API_ETMR_H

#include "result.h"

/**
 * @brief   Initialize the eTMR peripheral
 * @brief   初始化 eTMR 外设
 *
 * @details Pulls the eTMR configuration from board/etmr_config.c
 *          and applies it through the vendor SDK driver.
 *
 * @return  c02b2_result_t
 * @retval  C02B2_OK  Initialization succeeded
 */
c02b2_result_t Etmr_Init(void);

#endif /* C02B2_DRV_API_ETMR_H */
