/**
 * @file    rti.h
 * @brief   Real-time interrupt tick generator + caller-private period slot
 *
 * Hooked to SysTick at 1 ms resolution. Modules acquire a private
 * slot via `RTI_OpenSlot(period)` and check `RTI_SlotElapsed(&slot)`
 * each call. State is owned by the slot (no global flag variables
 * exposed), so multiple modules using the same period do NOT
 * collide.
 *
 * RTI_Defer (one-shot defer) lives in drv_api/rti_defer/ and uses
 * a separate 8-slot pool - it is unrelated to this periodic slot
 * API.
 */
#ifndef C02B2_RTI_H
#define C02B2_RTI_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief   Supported RTI sub-periods
 * @brief   支持的 RTI 子周期
 */
typedef enum {
    RTI_5MS    = 5,
    RTI_10MS   = 10,
    RTI_20MS   = 20,
    RTI_50MS   = 50,
    RTI_100MS  = 100,
    RTI_250MS  = 250,
    RTI_500MS  = 500,
    RTI_1000MS = 1000,
} rti_period_t;

/**
 * @brief   Caller-private period slot handle
 * @brief   调用者私有的周期 slot 句柄
 *
 * @details Opaque handle. Modules typically declare one as a
 *          file-scope static: `static rti_slot_t s_my_100ms;`
 *          and acquire it once in mcu_init().
 *
 *          A NULL `_priv` indicates an invalid handle (e.g. pool
 *          was full at RTI_OpenSlot time); RTI_SlotElapsed on
 *          such a handle returns false and emits no log (callers
 *          may check `_priv == NULL` explicitly).
 */
typedef struct {
    void *_priv;
} rti_slot_t;

/**
 * @brief   Total number of concurrent period slots supported
 * @brief   支持的并发 period slot 总数
 *
 * @details Default 64. Increase if a module pool-test reports
 *          pool-full warning. Each slot consumes ~12 bytes RAM.
 */
#ifndef RTI_SLOT_POOL_SIZE
  #define RTI_SLOT_POOL_SIZE  64u
#endif

/**
 * @brief   Initialize the RTI slot pool (called from RTI_Init)
 * @brief   初始化 RTI slot 池
 *
 * @note    Hardware SysTick configuration is performed by
 *          board/board_init.c; this only clears the pool.
 */
void RTI_Init(void);

/**
 * @brief   1 kHz tick callback invoked from the SysTick ISR
 * @brief   SysTick ISR 调用的 1kHz tick 回调
 *
 * @details Must be called from the SysTick ISR (1 kHz). The ISR
 *          is also responsible for clearing the SysTick interrupt
 *          flag and (optionally) feeding the watchdog.
 *
 * @note    Runs in ISR context; do not block.
 */
void RTI_OnTick1ms(void);

/**
 * @brief   Get the current 1 ms tick count
 * @brief   获取当前 1ms tick 计数
 *
 * @details Thin wrapper over OSIF_GetMilliseconds() so callers
 *          do not need to know which OSIF backend is in use.
 *
 * @return  uint32_t  Monotonic 1 ms tick (wraps after ~49 days)
 */
uint32_t RTI_GetTick1ms(void);

/**
 * @brief   Acquire a private slot bound to the given period
 * @brief   获取一个绑定指定周期的私有 slot
 *
 * @details The slot is reserved for the caller until the system
 *          resets. RTI_OpenSlot must be called from mcu_init or
 *          earlier - calling it from tick() works too but wastes
 *          the slot's first stamp.
 *
 *          The pool has RTI_SLOT_POOL_SIZE entries. When full,
 *          the returned handle has `_priv == NULL` and
 *          RTI_SlotElapsed() on it returns false silently.
 *
 * @param[in]  period  One of RTI_5MS..RTI_1000MS
 *
 * @return  rti_slot_t  Opaque handle; `_priv==NULL` if pool is full.
 */
rti_slot_t RTI_OpenSlot(rti_period_t period);

/**
 * @brief   Check whether the slot's period has elapsed
 * @brief   检查该 slot 的周期是否到期
 *
 * @details Each call updates the slot's last-fire timestamp
 *          independently. Multiple modules with the same period
 *          each get their own slot and never overwrite each other.
 *
 * @param[in,out]  slot  Handle obtained from RTI_OpenSlot.
 *                        NULL or invalid handles return false.
 *
 * @return  bool
 * @retval  true   Period elapsed (caller should run its sub-task)
 * @retval  false  Not yet elapsed, or handle invalid
 */
bool RTI_SlotElapsed(rti_slot_t *slot);

/**
 * @brief   Detect the first call after power-on or RTI_Init
 * @brief   检测上电或 RTI_Init 之后的第一次调用
 *
 * @details Useful for one-shot initialization inside a tick body
 *          without needing a separate `init_done` flag.
 *
 * @return  bool
 * @retval  true   First call (run the init branch)
 * @retval  false  Subsequent calls
 */
bool RTI_IsFirstCall(void);

#endif /* C02B2_RTI_H */
