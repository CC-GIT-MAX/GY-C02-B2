/**
 * @file    rti.c
 * @brief   RTI tick implementation backed by OSIF (SysTick)
 *
 * Tick source:
 *   - Normal operation: SysTick_Handler (in this file) drives the
 *     1 ms RTI tick via RTI_OnTick1ms(), feeds the watchdog, and
 *     calls osif_Tick() so that OSIF_GetMilliseconds() / OSIF_TimeDelay
 *     work too.
 *   - Low-power sleep: LPTMR remains initialised (drv_init) and
 *     could take over the tick source while the core is stopped;
 *     not used here.
 *
 * RTI_IsElapsed() / RTI_GetTick1ms() now read from OSIF instead of
 * a private counter - there is no double bookkeeping.
 */
#include "rti.h"
#include "rti_defer.h"
#include "osif.h"           /* OSIF_GetMilliseconds + osif_Tick */
#include "wdg_hw_access.h"  /* WDG_DRV_Trigger                  */

/**
 * @brief   Initialize RTI: start SysTick via OSIF and clear the
 *          one-shot deferred-callback pool.
 * @brief   初始化 RTI：通过 OSIF 启动 SysTick 并清空一次性延迟回调池
 *
 * @details Calling OSIF_TimeDelay(0) is the vendor-documented way to
 *          initialise SysTick without any side effects (it does not
 *          actually wait, but triggers osif_UpdateTickConfig() which
 *          programs SysTick->LOAD/CTRL). After this call the
 *          SysTick_Handler at the bottom of this file starts firing
 *          at 1 kHz.
 */
void RTI_Init(void)
{
    /* Start SysTick; the counter inside osif_Tick() is what we read
     * from RTI_GetTick1ms() below. */
    OSIF_TimeDelay(0);
    /* Clear the one-shot deferred-callback pool. */
    RTI_DeferInit();
}

/**
 * @brief   1 kHz tick callback invoked from the SysTick ISR
 * @brief   SysTick ISR 调用的 1kHz tick 回调
 */
void RTI_OnTick1ms(void)
{
    /* Intentionally empty: the SysTick_Handler at the bottom of this
     * file is the one true ISR that drives osif_Tick() + RTI_OnTick1ms
     * side-effects (watchdog feed). Kept as a hook so existing call
     * sites do not break. */
}

/**
 * @brief   Get the current 1 ms tick count
 * @brief   获取当前 1ms tick 计数
 *
 * @return  uint32_t  Monotonic 1 ms tick from OSIF
 */
uint32_t RTI_GetTick1ms(void)
{
    return OSIF_GetMilliseconds();
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
 * @brief   Check whether the requested period has elapsed since the last call
 * @brief   检查自上次调用以来指定周期是否已到
 */
bool RTI_IsElapsed(rti_period_t period)
{
    /* 8 static slots, one per supported period. */
    static uint32_t s_last[8] = {0};
    const uint32_t now = OSIF_GetMilliseconds();
    uint32_t slot;
    switch (period) {
        case RTI_5MS:    slot = 0; break;
        case RTI_10MS:   slot = 1; break;
        case RTI_20MS:   slot = 2; break;
        case RTI_50MS:    slot = 3; break;
        case RTI_100MS:  slot = 4; break;
        case RTI_250MS:  slot = 5; break;
        case RTI_500MS:  slot = 6; break;
        case RTI_1000MS: slot = 7; break;
        default:         return false;
    }
    if (now - s_last[slot] >= (uint32_t)period) {
        s_last[slot] = now;
        return true;
    }
    return false;
}

/**
 * @brief   1 ms SysTick ISR: drives OSIF tick, RTI tick, and WDG feed.
 * @brief   1ms SysTick 中断：驱动 OSIF 滴答、RTI 滴答、喂狗
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
