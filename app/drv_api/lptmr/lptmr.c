/**
 * @file    lptmr.c
 * @brief   LPTMR peripheral driver implementation
 * @brief   LPTMR 外设驱动实现
 *
 * 模块级初始化以及（未来可能新增的）公共辅助函数均在本文件实现。
 * 取代了较早的 app/drv_api/lptmr/lptmr_init.c。
 */
#include "lptmr.h"
#include "sdk_project_config.h"

#define MOD_NAME  "LPT"
#include "log.h"

c02b2_result_t Lptmr_Init(void)
{
    lpTMR_DRV_Init(0, &LPTMR_Config, false);
    return C02B2_OK;
}
