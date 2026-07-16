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

/**
 * @brief   Initialize the LPTMR peripheral
 * @brief   初始化 LPTMR 外设
 *
 * @return  c02b2_result_t    C02B2_OK: Initialization succeeded
 */
c02b2_result_t Lptmr_Init(void)
{
    lpTMR_DRV_Init(0, &LPTMR_Config, false);
    return C02B2_OK;
}

/**
 * @brief   Deinitialize the LPTMR peripheral
 * @brief   反初始化 LPTMR 外设
 *
 * @return  c02b2_result_t    C02B2_OK: Deinitialization succeeded
 */
c02b2_result_t Lptmr_Deinit(void)
{
    lpTMR_DRV_Deinit(0);
    return C02B2_OK;
}

/**
 * @brief   Start the LPTMR counter on the given instance
 * @brief   启动指定实例的 LPTMR 计数器
 *
 * @param[in]  instance  LPTMR instance index (currently always 0)
 */
void Lptmr_Start_Counter(u32 instance)
{
    lpTMR_DRV_StartCounter(instance);
}

/**
 * @brief   Stop the LPTMR counter on the given instance
 * @brief   停止指定实例的 LPTMR 计数器
 *
 * @param[in]  instance  LPTMR instance index (currently always 0)
 */
void Lptmr_Stop_Counter(u32 instance)
{
    lpTMR_DRV_StopCounter(instance);
}
