/**
 * @file    lptmr.h
 * @brief   LPTMR peripheral driver API
 * @brief   LPTMR 外设驱动接口
 *
 * 该单头文件同时暴露模块级初始化（Lptmr_Init()）
 * 与未来可能新增的公共辅助函数。本文件取代了较早的
 * app/drv_api/lptmr/lptmr_init.c + 缺失 .h 的布局。
 */
#ifndef C02B2_DRV_API_LPTMR_H
#define C02B2_DRV_API_LPTMR_H

#include "result.h"
#include "types.h"

/**
 * @brief   Initialize the LPTMR peripheral
 * @brief   初始化 LPTMR 外设
 *
 * @return  c02b2_result_t
 * @retval  C02B2_OK  Initialization succeeded
 */
c02b2_result_t Lptmr_Init(void);

/**
 * @brief   Deinitialize the LPTMR peripheral
 * @brief   反初始化 LPTMR 外设
 *
 * @return  c02b2_result_t
 * @retval  C02B2_OK  Deinitialization succeeded
 */
c02b2_result_t Lptmr_Deinit(void);

/**
 * @brief   Start the LPTMR counter on the given instance
 * @brief   启动指定实例的 LPTMR 计数器
 *
 * @param[in]  instance  LPTMR instance index (currently always 0)
 */
void Lptmr_Start_Counter(u32 instance);

/**
 * @brief   Stop the LPTMR counter on the given instance
 * @brief   停止指定实例的 LPTMR 计数器
 *
 * @param[in]  instance  LPTMR instance index (currently always 0)
 */
void Lptmr_Stop_Counter(u32 instance);

#endif /* C02B2_DRV_API_LPTMR_H */
