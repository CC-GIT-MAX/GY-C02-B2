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

static volatile uint32_t s_tick_ms = 0;

void RTI_Init(void)
{
    s_tick_ms = 0;
    /* LPTMR_Init is performed by board/board_init.c */
}

void RTI_OnTick1ms(void)
{
    s_tick_ms++;
}

uint32_t RTI_GetTick1ms(void)
{
    return s_tick_ms;
}

bool RTI_IsFirstCall(void)
{
    static uint32_t s_last = 0xFFFFFFFFu;
    bool first = (s_last == 0xFFFFFFFFu);
    s_last = s_tick_ms;
    return first;
}

bool RTI_IsElapsed(rti_period_t period)
{
    static uint32_t s_last[8] = {0};
    /* Map period to slot 0..7 */
    uint32_t slot;
    switch (period) {
        case RTI_5MS:    slot = 0; break;
        case RTI_10MS:   slot = 1; break;
        case RTI_20MS:   slot = 2; break;
        case RTI_50MS:   slot = 3; break;
        case RTI_100MS:  slot = 4; break;
        case RTI_250MS:  slot = 5; break;
        case RTI_500MS:  slot = 6; break;
        case RTI_1000MS: slot = 7; break;
        default:         return false;
    }
    if (s_tick_ms - s_last[slot] >= (uint32_t)period) {
        s_last[slot] = s_tick_ms;
        return true;
    }
    return false;
}
