/**
 * @file    power.h
 * @brief   Power management module
 *
 * Owner of:
 *   SIG_IGN_ON
 *   SIG_KL30_VOLTAGE_MV
 *   SIG_PWR_MODE
 *   SIG_IGN_OFF_COUNTER
 *   SIG_SLEEP_READY
 *
 * Implements the mod_desc_t four hooks and exposes a small
 * public API for events (e.g. CAN sleep) that need to
 * update readiness state.
 */
#ifndef LBX_MOD_POWER_H
#define LBX_MOD_POWER_H

#include "types.h"
#include "result.h"
#include "scheduler.h"

/**
 * @brief   Power-mode classification (used in SIG_PWR_MODE)
 * @brief   电源模式分类（用于 SIG_PWR_MODE）
 */
typedef enum {
    PWR_MODE_NORMAL = 0,
    PWR_MODE_UV1,        /* brownout warning                  */
    PWR_MODE_OV1,        /* overvoltage warning               */
    PWR_MODE_UV2,        /* < PWR_UV2_ENTER_MV, latch off    */
    PWR_MODE_OV2,        /* > PWR_OV2_ENTER_MV, latch off    */
    PWR_MODE_FAULT,
} pwr_mode_t;

#ifdef __cplusplus
extern "C" {
#endif

/* Public API for other modules / events */

/**
 * @brief   Notify the power module of an IGN edge (called from ISR)
 * @brief   通知电源模块发生 IGN 边沿（由 ISR 调用）
 *
 * @details Resets the debounce counter so that the tick context
 *          re-samples IGN immediately, rather than waiting up
 *          to PWR_IGN_DEBOUNCE_TICK * 10 ms.
 */
void       Power_OnIgnEdgeIrq(void);

/**
 * @brief   Signal CAN bus activity to keep the cluster awake
 * @brief   通知电源模块 CAN 总线有活动以保持唤醒
 *
 * @details Called by can_rx callbacks for every received frame;
 *          resets the IGN off counter and clears SIG_SLEEP_READY.
 */
void       Power_OnCanBusActivity(void);

/**
 * @brief   Mark the caller as a sleep blocker
 * @brief   将本调用方标记为阻止休眠
 *
 * @details Increments an internal reference counter; the
 *          cluster will not enter sleep while any caller holds
 *          a sleep request.
 */
void       Power_RequestSleep(void);

/**
 * @brief   Release a previously held sleep blocker
 * @brief   释放先前持有的休眠阻止请求
 */
void       Power_ClearSleepRequest(void);

/**
 * @brief   Get the current power-mode classification
 * @brief   获取当前电源模式分类
 *
 * @return  pwr_mode_t  Last classified mode
 */
pwr_mode_t Power_GetMode(void);

/**
 * @brief   Check whether IGN is currently considered ON
 * @brief   检查 IGN 当前是否处于 ON 状态
 *
 * @return  bool
 * @retval  true   IGN is ON (after debounce)
 * @retval  false  IGN is OFF or still debouncing
 */
bool       Power_IsIgnOn(void);

/**
 * @brief   Check whether the cluster is ready to enter sleep
 * @brief   检查本机是否可以进入休眠
 *
 * @return  bool
 * @retval  true   SIG_SLEEP_READY = 1
 * @retval  false  Some blocker is still active
 */
bool       Power_IsSleepReady(void);

/**
 * @brief   Module descriptor for mod_power (registered in scheduler.c)
 * @brief   mod_power 的模块描述符（在 scheduler.c 中注册）
 */
extern const mod_desc_t mod_power;

#ifdef __cplusplus
}
#endif

#endif /* LBX_MOD_POWER_H */
