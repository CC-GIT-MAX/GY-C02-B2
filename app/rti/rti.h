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
 * @details 不透明句柄。模块通常在文件作用域以 static 声明一个：
 *          `static rti_slot_t s_my_100ms;`，并由 mcu_init() 一次性获取。

 *          `_priv == NULL` 表示无效句柄（例如 RTI_OpenSlot 时池已满）；
 *          对该句柄调用 RTI_SlotElapsed 直接返回 false 且不记日志
 *          （调用方也可显式判断 `_priv == NULL`）。
 */
typedef struct {
    void *_priv;
} rti_slot_t;

/**
 * @brief   Total number of concurrent period slots supported
 * @brief   支持的并发 period slot 总数
 *
 * @details 默认 64。模块池满测试出现 warning 时增大。每个 slot
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
 * @details 必须在 SysTick ISR（1kHz）中调用。ISR 还负责清除 SysTick

 *          中断标志位，以及（可选）喂狗。
 * @note    Runs in ISR context; do not block.
 */
void RTI_OnTick1ms(void);

/**
 * @brief   Get the current 1 ms tick count
 * @brief   获取当前 1ms tick 计数
 *
 * @details 对 OSIF_GetMilliseconds() 的轻量封装，调用者无需
 *          关心当前用的是哪一种 OSIF 后端。
 * @return  uint32_t  Monotonic 1 ms tick (wraps after ~49 days)
 */
uint32_t RTI_GetTick1ms(void);

/**
 * @brief   Acquire a private slot bound to the given period
 * @brief   获取一个绑定指定周期的私有 slot
 *
 * @details 该 slot 被调用者独占，直到系统复位；0 = "上次从未见过"，
 *          冷/热复位后复用此语义。RTI_OpenSlot 必须在 mcu_init 或更早
 *          调用——若放到 tick() 里调用虽然能工作，但首次 stamp 被浪费。

 *          池共有 RTI_SLOT_POOL_SIZE 个槽位。池满时返回的句柄
 *          `_priv == NULL`，并对该句柄调用 RTI_SlotElapsed() 安静返回
 *          false。
 * @param[in]  period  One of RTI_5MS..RTI_1000MS
 *
 * @return  rti_slot_t  Opaque handle; `_priv==NULL` if pool is full.
 */
rti_slot_t RTI_OpenSlot(rti_period_t period);

/**
 * @brief   Check whether the slot's period has elapsed
 * @brief   检查该 slot 的周期是否到期
 *
 * @details 每次调用都会更新该 slot 的 last-fire 时间戳，
 *
 *          多个模块用同一 period 会各自分到独立 slot，互不覆盖。
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
 * @details 在 tick 体内用 `last_ms == 0` 同时承担"上电/复位后的
 *
 *          首次调用"标记，可省去额外的 `init_done` 标志位。
 * @return  bool
 * @retval  true   First call (run the init branch)
 * @retval  false  Subsequent calls
 */
bool RTI_IsFirstCall(void);

#endif /* C02B2_RTI_H */
