/**
 * @file    adc_init.c
 * @brief   Initialize ADC instance 0 (converter config only)
 */
#include "sdk_project_config.h"

/**
 * @brief   Initialize ADC instance 0
 *
 * @details Configures the converter hardware from board/adc_config.c.
 *          Channels are read on demand - no DMA or scan list is set up
 *          here yet.
 */
void Adc_Init(void)
{
    ADC_DRV_ConfigConverter(0, &adc_config0);
}
