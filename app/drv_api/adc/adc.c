/**
 * @file    adc.c
 * @brief   ADC peripheral driver implementation
 * @brief   ADC 外设驱动实现
 *
 * Module-level bring-up + (future) common-use helpers live here.
 * Replaces the older app/drv_api/adc/adc_init.c.
 */
#include "adc.h"
#include "sdk_project_config.h"

#define LOG_NAME  "ADC"
#include "log.h"

c02b2_result_t Adc_Init(void)
{
    ADC_DRV_ConfigConverter(0, &adc_config0);
    return C02B2_OK;
}
