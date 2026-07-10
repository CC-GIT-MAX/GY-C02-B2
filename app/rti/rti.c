/**
 * @file    rti.c
 * @brief   RTI tick implementation backed by OSIF (SysTick) +
 *          caller-private period slot pool.
 *
 * Tick source:
 *   - SysTick_Handler at the bottom of this file drives the 1 ms
 *     RTI tick via RTI_OnTick1ms(), feeds the watchdog, and
 *     calls osif_Tick() so that OSIF_GetMilliseconds() works too.
 *
 * Slot pool:
 *   - s_slots[RTI_SLOT_POOL_SIZE] is a static pool of private
 *     period slots. RTI_OpenSlot() scans for the first free slot
 *     and stamps it. RTI_SlotElapsed() compares against the slot's
 *     own last timestamp - no global flag, no collision.
 */
#include "rti.h"
#include "drv_api/rti_defer/rti_defer.h"
#include "osif.h"
#include "wdg_hw_access.h"

/**
 * @brief   Internal slot descriptor
 * @brief   内部 slot 描述符
 */
typedef struct {
    uint16_t period;    /**< RTI_5MS..RTI_1000MS, 0 = free */
    uint16_t inited;    /**< 1 = slot acquired and stamped */
    uint32_t last_ms;   /**< OSIF tick at last fire */
} rti_slot_desc_t;

/** Static pool - no malloc on the bare-metal target. */
static rti_slot_desc_t s_slots[RTI_SLOT_POOL_SIZE];

/**
 * @brief   Initialize RTI: clear the slot pool, start SysTick,
 *          and clear the deferred-callback pool.
 * @brief   初始化 RTI:清空 slot 池,启动 SysTick,清空延后回调池
 */
void RTI_Init(void)
{
    /* Clear the slot pool. */
    for (uint32_t i = 0; i < RTI_SLOT_POOL_SIZE; i++) {
        s_slots[i].period  = 0u;
        s_slots[i].inited  = 0u;
        s_slots[i].last_ms = 0u;
    }
    /* Start SysTick; OSIF_TimeDelay(0) primes the tick config
     * without actually waiting. */
    OSIF_TimeDelay(0);
    /* Clear the one-shot deferred-callback pool. */
    RTI_DeferInit();
}

/**
 * @brief   1 kHz tick callback invoked from the SysTick ISR
 * @brief   SysTick ISR 调用的 1kHz tick 回调
 *
 * @details Intentionally empty: the SysTick_Handler at the
 *          bottom of this file is the one true ISR that drives
 *          osif_Tick() + RTI_OnTick1ms side-effects (watchdog
 *          feed). Kept as a hook so existing call sites do not
 *          break.
 */
void RTI_OnTick1ms(void)
{
    /* No-op: side effects live in SysTick_Handler. */
}

/**
 * @brief   Get the current 1 ms tick count
 * @brief   获取当前 1ms tick 计数
 */
uint32_t RTI_GetTick1ms(void)
{
    return OSIF_GetMilliseconds();
}

/**
 * @brief   Acquire a private slot bound to the given period
 * @brief   获取一个绑定指定周期的私有 slot
 */
rti_slot_t RTI_OpenSlot(rti_period_t period)
{
    rti_slot_t out = { NULL };
    for (uint32_t i = 0; i < RTI_SLOT_POOL_SIZE; i++) {
        if (s_slots[i].inited == 0u) {
            s_slots[i].period  = (uint16_t)period;
            s_slots[i].inited  = 1u;
            s_slots[i].last_ms = OSIF_GetMilliseconds();
            /* Encode slot index as (i+1) in _priv so that 0 is
             * unambiguously "invalid handle". */
            out._priv = (void *)(uintptr_t)(i + 1u);
            return out;
        }
    }
    return out; /* pool full - returns { NULL } */
}

/**
 * @brief   Check whether the slot's period has elapsed
 * @brief   检查该 slot 的周期是否到期
 */
bool RTI_SlotElapsed(rti_slot_t *slot)
{
    if (slot == NULL) { return false; }
    uintptr_t idx_plus1 = (uintptr_t)slot->_priv;
    if (idx_plus1 == 0u || idx_plus1 > RTI_SLOT_POOL_SIZE) {
        return false;
    }
    uint32_t i = (uint32_t)(idx_plus1 - 1u);
    rti_slot_desc_t *s = &s_slots[i];
    if (s->inited == 0u) {
        return false;
    }
    const uint32_t now = OSIF_GetMilliseconds();
    if ((now - s->last_ms) >= (uint32_t)s->period) {
        s->last_ms = now;
        return true;
    }
    return false;
}

/**
 * @brief   Detect the first call after power-on or RTI_Init
 * @brief   检测上电或 RTI_Init 之后的第一次调用
 */
bool RTI_IsFirstCall(void)
{
    static uint32_t s_last = 0xFFFFFFFFu;
    const uint32_t now = OSIF_GetMilliseconds();
    bool first = (s_last == 0xFFFFFFFFu);
    s_last = now;
    return first;
}

/**
 * @brief   1 ms SysTick ISR: drives OSIF tick, RTI tick, and WDG feed.
 * @brief   1ms SysTick 中断:驱动 OSIF 滴答、RTI 滴答、喂狗
 *
 * @details Single authoritative source of all 1 ms side-effects.
 *          Owns three responsibilities:
 *            1. osif_Tick()        - increments s_osif_tick_cnt so
 *                                     OSIF_GetMilliseconds /
 *                                     OSIF_TimeDelay reflect real time.
 *            2. RTI_OnTick1ms()    - feeds the RTI scheduler side.
 *            3. WDG_DRV_Trigger(0) - feeds the watchdog so a hung
 *                                     super-loop resets the MCU.
 */
void SysTick_Handler(void)
{
    osif_Tick();
    RTI_OnTick1ms();
    WDG_DRV_Trigger(0);
}
