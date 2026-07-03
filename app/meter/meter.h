/**
 * @file    meter.h
 * @brief   Pointer meter module (speed / RPM / fuel / temp)
 *
 * Reads values from the signal bus (populated by mod_can_rx or
 * the ADC poll), runs a low-pass filter and movement smoothing,
 * and drives the stepper-motor PWM outputs.
 *
 * Owner of: (none — meter is a pure consumer)
 *   It only publishes nothing; the outputs are the stepper
 *   target values written via Meter_SetTarget().
 */
#ifndef LBX_MOD_METER_H
#define LBX_MOD_METER_H

#include "types.h"
#include "result.h"
#include "scheduler.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Per-gauge descriptor */
typedef struct {
    u16 now;       /* current steps in motor                    */
    u16 target;    /* desired steps (after smoothing)           */
    u16 max;       /* physical steps, e.g. 240 for speedo       */
    u8  speed;     /* movement speed (steps per tick)           */
} meter_gauge_t;

typedef enum {
    METER_GAUGE_SPEED = 0,
    METER_GAUGE_RPM,
    METER_GAUGE_FUEL,
    METER_GAUGE_TEMP,
    METER_GAUGE_MAX,
} meter_gauge_id_t;

/* Test/diag API */
lbx_result_t Meter_SetDiagTarget(meter_gauge_id_t id, u16 target);
u16          Meter_GetTarget(meter_gauge_id_t id);
u16          Meter_GetNow(meter_gauge_id_t id);

/* Module descriptor */
extern const mod_desc_t mod_meter;

#ifdef __cplusplus
}
#endif

#endif /* LBX_MOD_METER_H */
