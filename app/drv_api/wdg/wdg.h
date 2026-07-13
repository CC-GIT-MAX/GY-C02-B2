/**
 * @file    wdg.h
 * @brief   Watchdog (WDG) wrapper API
 * @brief   看门狗封装接口
 *
 * 对厂商 wdg_driver 的薄封装。Wdg_Init() 从 board/wdg_config.c
 * 取出配置并运行 SDK 初始化；Wdg_Trigger() 由超循环在每个主循环
 * 周期调用一次，用于"喂狗"。
 */
#ifndef C02B2_DRV_API_WDG_H
#define C02B2_DRV_API_WDG_H

#include "types.h"
#include "result.h"

/**
 * @brief   Initialize the WDG peripheral
 * @brief   初始化看门狗
 *
 * @details 从 board/wdg_config.c 取出配置，
 *          通过 SDK 驱动应用之。在 DRV_Init() 中调用一次。
 *
 * @return  c02b2_result_t
 * @retval  C02B2_OK  Init succeeded
 * @retval  C02B2_ERR SDK init failed
 */
c02b2_result_t Wdg_Init(void);

/**
 * @brief   Trigger / refresh the watchdog counter
 * @brief   喂狗
 *
 * @details 在主超循环（Scheduler_Run 或等价物）中
 *          每个主循环周期调用一次。漏掉刷新窗口将
 *          复位 MCU。
 */
void Wdg_Trigger(void);

/**
 * @brief   Read the current watchdog counter value
 * @brief   读取看门狗当前计数值
 *
 * @return  u32  Current down-counter value
 */
u32 Wdg_GetCounter(void);

#endif /* C02B2_DRV_API_WDG_H */
