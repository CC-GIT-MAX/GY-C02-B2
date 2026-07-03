/**
 * @file    rti.h
 * @brief   Real-time interrupt tick generator and period flags
 *
 * Hooked to LPTMR or SysTick at 1 ms resolution. Modules register a
 * period (5 / 10 / 20 / 50 / 100 / 250 / 500 / 1000 ms) and check
 * RTI_IsElapsed(period) each call. State is owned by the module
 * (no global flag variables exposed).
 */
#ifndef LBX_RTI_H
#define LBX_RTI_H

#include <stdbool.h>
#include <stdint.h>

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

void     RTI_Init(void);
void     RTI_OnTick1ms(void);          /* Called from LPTMR ISR @ 1kHz */
uint32_t RTI_GetTick1ms(void);
bool     RTI_IsElapsed(rti_period_t period);  /* Caller owns last-run tick */
bool     RTI_IsFirstCall(void);

#endif /* LBX_RTI_H */
