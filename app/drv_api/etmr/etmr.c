/**
 * @file    etmr.c
 * @brief   eTMR peripheral driver implementation
 * @brief   eTMR 外设驱动实现
 *
 * 模块级初始化以及（未来可能新增的）公共辅助函数均在本文件实现。
 * 取代了较早的 app/drv_api/etmr/etmr_init.c。
 */
#include "etmr.h"
#include "sdk_project_config.h"

#define LOG_NAME  "ETM"
#include "log.h"

c02b2_result_t Etmr_Init(void)
{
    eTMR_DRV_Init(0, &ETMR_CM_Config0, &ETMR_CM_Config0_State);
    eTMR_DRV_InitPwm(3, &ETMR3_PWM_Config0);
    return C02B2_OK;
}
