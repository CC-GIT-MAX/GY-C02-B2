/**
 * @file    adc.h
 * @brief   ADC peripheral driver API
 * @brief   ADC 外设驱动接口
 *
 * 该单头文件同时暴露模块级初始化（Adc_Init()）
 * 与未来可能新增的公共辅助函数。本文件取代了较早的
 * app/drv_api/adc/adc_init.c + 缺失 .h 的布局。
 */
#ifndef C02B2_DRV_API_ADC_H
#define C02B2_DRV_API_ADC_H

#include "result.h"

/**
 * @brief   Initialize the ADC peripheral
 * @brief   初始化 ADC 外设
 *
 * @details 从 board/adc_config.c 取出 ADC 配置，
 *          并通过厂商 SDK 驱动应用之。
 *
 * @return  c02b2_result_t
 * @retval  C02B2_OK  Initialization succeeded
 */
c02b2_result_t Adc_Init(void);

#endif /* C02B2_DRV_API_ADC_H */
