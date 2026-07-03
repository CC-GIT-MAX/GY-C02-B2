/**
 * @file    signal.h
 * @brief   Central signal bus for inter-module communication
 *
 * Modules do NOT exchange data via extern globals. They publish/consume
 * through Signal_Set / Signal_Get. This keeps ownership clear and makes
 * modules individually testable.
 *
 * Signals are statically enumerated; no dynamic registration.
 */
#ifndef LBX_SIGNAL_H
#define LBX_SIGNAL_H

#include <stdbool.h>
#include <stdint.h>
#include "result.h"

typedef enum {
    SIG_INVALID = 0,

    /* Power / ignition */
    SIG_IGN_ON,            /* bool, 1 = KL15 on */
    SIG_KL30_VOLTAGE_MV,   /* int32, KL30 voltage in mV */
    SIG_PWR_MODE,          /* int32, see pwr_mode_t */
    SIG_GC_POWER_ON,       /* bool */

    /* Vehicle */
    SIG_VEH_SPEED_KPH_X10, /* int32, 0.1 kph */
    SIG_ENG_RPM,           /* int32, rpm */
    SIG_FUEL_LEVEL_PCT,    /* int32, 0..100 */
    SIG_COOLANT_TEMP_C,    /* int32, degC */
    SIG_ODO_TOTAL_M,       /* int32, total odometer in meters */

    /* Telltale / display */
    SIG_TT_LEFT_TURN,      /* bool */
    SIG_TT_RIGHT_TURN,     /* bool */
    SIG_TT_HIGH_BEAM,      /* bool */
    SIG_TT_LOW_BEAM,       /* bool */
    /* ... add new signals here ... */

    SIG_MAX
} signal_id_t;

lbx_result_t Signal_Set(signal_id_t id, int32_t value);
int32_t      Signal_Get(signal_id_t id);
bool         Signal_IsValid(signal_id_t id);
void         Signal_Invalidate(signal_id_t id);

#endif /* LBX_SIGNAL_H */
