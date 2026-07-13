/**
 * @file    adc.c
 * @brief   ADC peripheral driver implementation
 * @brief   ADC 外设驱动实现
 *
 * 模块级初始化以及（未来可能新增的）公共辅助函数均在本文件实现。
 * 取代了较早的 app/drv_api/adc/adc_init.c。
 */
#include "adc.h"
#include "sdk_project_config.h"

#define MOD_NAME  "ADC"
#include "log.h"

c02b2_result_t Adc_Init(void)
{
    ADC_DRV_ConfigConverter(0, &adc_config0);
    return C02B2_OK;
}
