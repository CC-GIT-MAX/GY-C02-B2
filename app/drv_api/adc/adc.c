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
uint16 YTM_AD_READ(uint8 channel) 
{
    uint16 result=0;
    uint32 instance=0;
    // 选择通道
    ADC0->CHSEL[0]=channel;
    ADC0->STS|=ADC_STS_EOSEQ_MASK;
    // 启动转换
    ADC_DRV_Start(instance); 
    // 等待AD转换完成
    while(!ADC_DRV_GetEndOfSequenceFlag(instance) && result<25000)  result++;
    // ADC错误初始化
    if(result>=25000) 
    {
        ADC_DRV_ConfigConverter(instance,&adc_config0);
    }
    // 读取转换结果
    result=ADC0->FIFO;
    //返回转换结果
    return result;
}