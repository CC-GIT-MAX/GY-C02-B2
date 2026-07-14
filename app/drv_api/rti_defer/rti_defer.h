/**
 * @file    rti_defer.h
 * @brief   One-shot deferred execution on the RTI tick
 *
 * 需要在"N 毫秒后"执行一段代码而又不想忙等的模块使用本 API：
 *
 *   - RTI_Defer(ms, cb, ctx)  注册一次性回调
 *   - RTI_DeferCancel(cb, ctx) 取消尚未触发的槽位
 *   - RTI_DeferTick() 在每次超循环迭代中调用一次
 *     （Scheduler_Run() 负责调用），分发所有已到期的槽位。
 *
 * 实现内部固定拥有 8 个槽位 —— 无 malloc、不在 ISR 上下文、
 * 无优先级继承。 Good enough for
 * ignition debouncing, "send then check ACK 50 ms later",
 * power-mode cool-down, etc.
 *
 * Threading: tick context only. Never call RTI_Defer() or
 * RTI_DeferCancel() from an ISR - the slot scan is not lock-free.
 */
#ifndef C02B2_RTI_DEFER_H
#define C02B2_RTI_DEFER_H

#include <stdint.h>
#include "result.h"

/**
 * @brief   Deferred-callback signature
 * @brief   延后回调函数原型
 *
 * @param[in,out] ctx  Opaque context pointer registered alongside the callback.
 *                     The caller decides what this points to (module state,
 *                     a stack-local struct, a heap object, ...). The pointer
 *                     is stored verbatim; RTI never dereferences it.
 */
typedef void (*rti_defer_cb_t)(void *ctx);

/**
 * @brief   Number of deferred slots in the static pool
 * @brief   静态延后槽数量
 */
#define RTI_DEFER_SLOTS  8u

/**
 * @brief   Register a one-shot callback to fire `delay_ms` from now
 * @brief   注册一个延后 `delay_ms` 毫秒后触发的一次性回调
 *
 * @details 槽位从调用起被占用，直到回调触发
 *          （或被显式取消）为止。使用相同 (cb, ctx)
 *          元组的连续调用会替换 deadline ——
 *          便于"通过再注册实现去抖"。
 *
 *          delay_ms 被解读为相对 RTI_GetTick1ms() 的
 *          u32 单调增量；可能溢出 u32 的值会被钳制为 0xFFFFFFFF。
 *
 * @param[in]  delay_ms  Milliseconds from now (0 = fire on the
 *                       next RTI_DeferTick())
 * @param[in]  cb        Callback to invoke. Must be non-NULL.
 * @param[in]  ctx       Caller-owned context. Stored verbatim.
 *
 * @return  c02b2_result_t
 * @retval  C02B2_OK             Slot allocated, callback armed
 * @retval  C02B2_ERR_PARAM      cb is NULL or delay_ms > 0 but
 *                               (cb, ctx) is the sentinel pair
 * @retval  C02B2_ERR_OVERFLOW       All 8 slots are busy and no slot
 *                               matches (cb, ctx); nothing was armed
 * @retval  C02B2_ERR_NOT_READY  RTI is not initialised
 */
c02b2_result_t RTI_Defer(uint32_t delay_ms, rti_defer_cb_t cb, void *ctx);

/**
 * @brief   Cancel a pending callback
 * @brief   取消一个尚未触发的回调
 *
 * @details 在池中查找 (cb, ctx) 匹配的槽位并置为空。
 *          无匹配时为 no-op。
 *
 * @param[in]  cb   Callback that was passed to RTI_Defer
 * @param[in]  ctx  Context that was passed to RTI_Defer
 *
 * @return  c02b2_result_t
 * @retval  C02B2_OK             A slot was found and freed
 * @retval  C02B2_ERR_NOT_FOUND  No matching slot is pending
 * @retval  C02B2_ERR_PARAM      cb is NULL
 */
c02b2_result_t RTI_DeferCancel(rti_defer_cb_t cb, void *ctx);

/**
 * @brief   Dispatch any callbacks whose deadline has elapsed
 * @brief   分发所有已到期的回调
 *
 * @details 由 Scheduler_Run() 在每次超循环 tick 中调用一次。
 *          对每个触发的槽位：在回调执行前将其置为空，
 *          这样回调内部重入的 RTI_Defer() 调用即可获得空闲槽位，
 *          新 deadline 将在同一 tick 内被观察到。
 *
 *          回调执行顺序为槽位下标升序（下标最小者先执行）。
 *          不会在 ISR 上下文调用任何回调。
 */
void RTI_DeferTick(void);

/**
 * @brief   Reset the deferred-callback pool (cold-boot helper)
 * @brief   重置延后回调池（冷启动辅助）
 *
 * @details 由 RTI_Init() 调用，用于清空热启动后遗留的
 *          陈旧条目。池为空时调用也是安全的。
 */
void RTI_DeferInit(void);

/**
 * @brief   Number of currently-armed deferred slots
 * @brief   当前已占用槽位数（for tests / diagnostics）
 *
 * @return  uint32_t  Count in [0, RTI_DEFER_SLOTS]
 */
uint32_t RTI_DeferPending(void);

#endif /* C02B2_RTI_DEFER_H */