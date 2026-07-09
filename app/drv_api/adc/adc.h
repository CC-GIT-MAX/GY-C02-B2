/**
 * @file    adc.h
 * @brief   ADC peripheral driver API
 * @brief   ADC 外设驱动接口
 *
 * Single header that exposes both the module-level bring-up
 * (Adc_Init()) and any future common-use helpers.  This file
 * replaces the older app/drv_api/adc/adc_init.c + (missing .h)
 * layout.
 */
#ifndef C02B2_DRV_API_ADC_H
#define C02B2_DRV_API_ADC_H

#include "result.h"

/**
 * @brief   Initialize the ADC peripheral
 * @brief   初始化 ADC 外设
 *
 * @details Pulls the ADC configuration from board/adc_config.c
 *          and applies it through the vendor SDK driver.
 *
 * @return  c02b2_result_t
 * @retval  C02B2_OK  Initialization succeeded
 */
c02b2_result_t Adc_Init(void);

#endif /* C02B2_DRV_API_ADC_H */
