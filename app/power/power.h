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
void       Power_OnIgnEdgeIrq(void);   /* called from external interrupt */
void       Power_OnCanBusActivity(void);
void       Power_RequestSleep(void);   /* mark not ready to sleep */
void       Power_ClearSleepRequest(void);
pwr_mode_t Power_GetMode(void);
bool       Power_IsIgnOn(void);
bool       Power_IsSleepReady(void);

extern const mod_desc_t mod_power;

#ifdef __cplusplus
}
#endif

#endif /* LBX_MOD_POWER_H */
