/**
 * @file    wdg.h
 * @brief   Watchdog (WDG) wrapper API
 * @brief   看门狗封装接口
 *
 * Thin wrapper around the vendor wdg_driver.  Wdg_Init() runs the
 * SDK init from board/wdg_config.c; Wdg_Trigger() is what the
 * super-loop should call once per major cycle to keep the dog fed.
 */
#ifndef C02B2_DRV_API_WDG_H
#define C02B2_DRV_API_WDG_H

#include "types.h"
#include "result.h"

/**
 * @brief   Initialize the WDG peripheral
 * @brief   初始化看门狗
 *
 * @details Pulls the config from board/wdg_config.c and applies it
 *          through the SDK driver.  Call once from DRV_Init().
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
 * @details Call this from the main super-loop (Scheduler_Run or
 *          equivalent) on every major cycle.  Skipping a refresh
 *          window resets the MCU.
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
