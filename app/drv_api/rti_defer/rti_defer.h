/**
 * @file    rti_defer.h
 * @brief   One-shot deferred execution on the RTI tick
 *
 * Modules that need to run a piece of code "N milliseconds from
 * now" without busy-waiting use this API:
 *
 *   - RTI_Defer(ms, cb, ctx)  registers a one-shot callback
 *   - RTI_DeferCancel(cb, ctx) cancels a still-pending slot
 *   - RTI_DeferTick() is invoked once per super-loop iteration
 *     (Scheduler_Run() does this) and dispatches any slot whose
 *     deadline has been reached.
 *
 * The implementation owns a fixed pool of 8 slots - no malloc,
 * no ISR context, no priority inheritance. Good enough for
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
 * @details The slot is occupied from the call until the callback
 *          fires (or it is explicitly cancelled). A subsequent
 *          call with the same (cb, ctx) tuple REPLACES the
 *          deadline - useful for "debounce by re-arming".
 *
 *          delay_ms is interpreted as a u32 monotonic delta
 *          against RTI_GetTick1ms(); values that would overflow
 *          the u32 are clamped to 0xFFFFFFFF.
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
 * @details Scans the pool for a slot whose (cb, ctx) matches and
 *          marks it free. No-op if nothing matches.
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
 * @details Called from Scheduler_Run() once per super-loop tick.
 *          For each fired slot: the slot is marked free BEFORE
 *          the callback runs, so a re-entrant RTI_Defer() call
 *          from inside the callback gets a free slot and the new
 *          deadline is observed in the same tick.
 *
 *          CB execution order is slot index order (lowest first).
 *          No callback is called from ISR context.
 */
void RTI_DeferTick(void);

/**
 * @brief   Reset the deferred-callback pool (cold-boot helper)
 * @brief   重置延后回调池（冷启动辅助）
 *
 * @details Called from RTI_Init() to clear any stale entries
 *          left over from a warm boot. Safe to call when the
 *          pool is already empty.
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