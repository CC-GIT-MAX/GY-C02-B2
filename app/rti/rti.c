/**
 * @file    rti.c
 * @brief   RTI tick implementation
 *
 * NOTE: Hardware-specific. Project must:
 *   1. Configure LPTMR to fire @ 1 kHz (or use SysTick).
 *   2. In the ISR, call RTI_OnTick1ms() then clear the LPTMR flag.
 *   3. WDG feed is handled inside the ISR or by scheduler.
 */
#include "rti.h"
#include "rti_defer.h"

/* Monotonic 1 ms counter. `volatile` because the ISR writes it. */
static volatile uint32_t s_tick_ms = 0;

/**
 * @brief   Initialize the 1 ms RTI counter
 * @brief   初始化 1ms RTI 计数器
 */
void RTI_Init(void)
{
    s_tick_ms = 0;
    /* LPTMR_Init is performed by board/board_init.c. */
    /* Clear the one-shot deferred-callback pool too. */
    RTI_DeferInit();
}

/**
 * @brief   1 kHz tick callback invoked from the LPTMR ISR
 * @brief   LPTMR ISR 调用的 1kHz tick 回调
 *
 * @details Bare-bones: only bumps the counter. Any work that
 *          needs to happen every 1 ms should use RTI_IsElapsed().
 */
void RTI_OnTick1ms(void)
{
    s_tick_ms++;
}

/**
 * @brief   Get the current 1 ms tick count
 * @brief   获取当前 1ms tick 计数
 *
 * @return  uint32_t  Monotonic 1 ms tick
 */
uint32_t RTI_GetTick1ms(void)
{
    return s_tick_ms;
}

/**
 * @brief   Detect the first call after power-on or RTI_Init
 * @brief   检测上电或 RTI_Init 之后的第一次调用
 *
 * @details Trick: keep a `sentinel` last-tick value (0xFFFFFFFF)
 *          that can never match a real tick. The first call always
 *          sees the sentinel and returns true; subsequent calls
 *          see the saved tick and return false.
 */
bool RTI_IsFirstCall(void)
{
    static uint32_t s_last = 0xFFFFFFFFu;
    bool first = (s_last == 0xFFFFFFFFu);
    /* Update last-tick to the current real counter for next call. */
    s_last = s_tick_ms;
    return first;
}

/**
 * @brief   Check whether the requested period has elapsed since the last call
 * @brief   检查自上次调用以来指定周期是否已到
 *
 * @details Each period enum value maps to a slot in a static
 *          last-run array; the same slot is shared between all
 *          callers using that period, so this helper is best
 *          for "only one module needs this period" use cases.
 *
 * @param[in]  period  One of RTI_5MS..RTI_1000MS
 *
 * @return  bool
 * @retval  true   Period elapsed
 * @retval  false  Not yet elapsed
 */
bool RTI_IsElapsed(rti_period_t period)
{
    /* 8 static slots, one per supported period. */
    static uint32_t s_last[8] = {0};
    /* Map the period enum to its slot index. */
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
    /* Unsigned subtraction is wrap-safe; >= period means "elapsed". */
    if (s_tick_ms - s_last[slot] >= (uint32_t)period) {
        /* Stamp the new last-run tick for the next round. */
        s_last[slot] = s_tick_ms;
        return true;
    }
    return false;
}
