/**
 * @file    meter.c
 * @brief   Pointer meter module
 *
 *  1. Each 20ms tick, read raw values from the signal bus.
 *  2. Apply a low-pass filter and clamp to [0, max].
 *  3. Move `now` toward `target` at the gauge's `speed` steps/tick
 *     (rise faster than fall, typical automotive behavior).
 *  4. Drive the stepper PWM output (TODO, calls into YTM_PWM_OUT).
 *  5. Diag path bypasses the filter and forces `target` directly.
 */
#include "meter.h"
#include "rti.h"
#include "signal.h"

#define LOG_NAME  "MET "
#include "log.h"

/* Per-gauge config: max steps and movement speed */
#define MTR_SPD_MAX_STEPS     240u
#define MTR_SPD_SPEED_RISE    6u    /* steps per 20ms tick going up   */
#define MTR_SPD_SPEED_FALL    4u

#define MTR_RPM_MAX_STEPS     80u
#define MTR_RPM_SPEED_RISE    4u
#define MTR_RPM_SPEED_FALL    3u

#define MTR_FUEL_MAX_STEPS    54u
#define MTR_FUEL_SPEED_RISE   1u
#define MTR_FUEL_SPEED_FALL   1u

#define MTR_TEMP_MAX_STEPS    54u
#define MTR_TEMP_SPEED_RISE   1u
#define MTR_TEMP_SPEED_FALL   1u

/* Per-gauge state */
typedef struct {
    u16 now;
    u16 target;
    u16 max;
    u8  rise_speed;
    u8  fall_speed;
    bool diag_hold;        /* diag forces target, movement still smooth */
} gauge_state_t;

static struct {
    bool init_done;
    gauge_state_t gauges[METER_GAUGE_MAX];
} s_m;

static const struct {
    u16 max;
    u8  rise;
    u8  fall;
} k_cfg[METER_GAUGE_MAX] = {
    [METER_GAUGE_SPEED] = { MTR_SPD_MAX_STEPS, MTR_SPD_SPEED_RISE, MTR_SPD_SPEED_FALL },
    [METER_GAUGE_RPM]   = { MTR_RPM_MAX_STEPS, MTR_RPM_SPEED_RISE, MTR_RPM_SPEED_FALL },
    [METER_GAUGE_FUEL]  = { MTR_FUEL_MAX_STEPS, MTR_FUEL_SPEED_RISE, MTR_FUEL_SPEED_FALL },
    [METER_GAUGE_TEMP]  = { MTR_TEMP_MAX_STEPS, MTR_TEMP_SPEED_RISE, MTR_TEMP_SPEED_FALL },
};

/* Helpers --------------------------------------------------------------- */
static u16 prv_read_target(meter_gauge_id_t id)
{
    /* Read raw physical quantity from the signal bus, then
     * scale to gauge steps. The mapping is intentionally simple;
     * real projects would do a calibration LUT here. */
    switch (id) {
        case METER_GAUGE_SPEED: {
            /* SIG_VEH_SPEED_KPH_X10: 0.1 kph.  Scale: 240 steps / 260 kph */
            int32_t raw = Signal_Get(SIG_VEH_SPEED_KPH_X10);
            if (raw < 0) raw = 0;
            u32 v = ((u32)raw * MTR_SPD_MAX_STEPS) / 2600u;
            return (u16)(v > MTR_SPD_MAX_STEPS ? MTR_SPD_MAX_STEPS : v);
        }
        case METER_GAUGE_RPM: {
            /* SIG_ENG_RPM: rpm.  Scale: 80 steps / 8000 rpm */
            int32_t raw = Signal_Get(SIG_ENG_RPM);
            if (raw < 0) raw = 0;
            u32 v = ((u32)raw * MTR_RPM_MAX_STEPS) / 8000u;
            return (u16)(v > MTR_RPM_MAX_STEPS ? MTR_RPM_MAX_STEPS : v);
        }
        case METER_GAUGE_FUEL: {
            int32_t raw = Signal_Get(SIG_FUEL_LEVEL_PCT);
            if (raw < 0)   raw = 0;
            if (raw > 100) raw = 100;
            return (u16)(((u32)raw * MTR_FUEL_MAX_STEPS) / 100u);
        }
        case METER_GAUGE_TEMP: {
            /* Map -40..140 degC to 0..54 steps */
            int32_t raw = Signal_Get(SIG_COOLANT_TEMP_C);
            if (raw <  -40) raw = -40;
            if (raw >  140) raw = 140;
            u32 v = ((u32)(raw + 40) * MTR_TEMP_MAX_STEPS) / 180u;
            return (u16)(v > MTR_TEMP_MAX_STEPS ? MTR_TEMP_MAX_STEPS : v);
        }
        default:
            return 0u;
    }
}

static void prv_drive_motor(meter_gauge_id_t id)
{
    gauge_state_t *g = &s_m.gauges[id];
    if (g->now < g->target) {
        u16 delta = g->target - g->now;
        g->now = (delta <= g->rise_speed) ? g->target : (g->now + g->rise_speed);
    } else if (g->now > g->target) {
        u16 delta = g->now - g->target;
        g->now = (delta <= g->fall_speed) ? g->target : (g->now - g->fall_speed);
    }
    /* TODO: feed g->now to eTMR PWM driver for the gauge stepper */
    (void)id;
}

static void prv_tick_one(meter_gauge_id_t id)
{
    gauge_state_t *g = &s_m.gauges[id];
    if (!g->diag_hold) {
        g->target = prv_read_target(id);
        if (g->target > g->max) g->target = g->max;
    }
    prv_drive_motor(id);
}

/* mod_desc_t hooks ----------------------------------------------------- */
static void prv_init(u8 cold_boot)
{
    (void)cold_boot;
    for (u32 i = 0; i < METER_GAUGE_MAX; i++) {
        s_m.gauges[i].now       = 0u;
        s_m.gauges[i].target    = 0u;
        s_m.gauges[i].max       = k_cfg[i].max;
        s_m.gauges[i].rise_speed = k_cfg[i].rise;
        s_m.gauges[i].fall_speed = k_cfg[i].fall;
        s_m.gauges[i].diag_hold  = false;
    }
    s_m.init_done = true;
    LOG_I("init (cold=%u): 4 gauges", (unsigned)cold_boot);
}

static void prv_on_ign_on(void)
{
    /* Re-zero pointers on IGN ON for safety (homing is board-specific) */
    for (u32 i = 0; i < METER_GAUGE_MAX; i++) {
        s_m.gauges[i].now    = 0u;
        s_m.gauges[i].target = 0u;
    }
    LOG_I("on_ign_on: pointers zeroed");
}

static void prv_tick(void)
{
    if (!s_m.init_done) return;
    if (RTI_IsElapsed(RTI_20MS)) {
        prv_tick_one(METER_GAUGE_SPEED);
        prv_tick_one(METER_GAUGE_RPM);
        prv_tick_one(METER_GAUGE_FUEL);
        prv_tick_one(METER_GAUGE_TEMP);
    }
}

static void prv_standby(void)
{
    for (u32 i = 0; i < METER_GAUGE_MAX; i++) {
        s_m.gauges[i].target = 0u;
    }
    LOG_I("standby: targets zeroed");
}

/* Public API ------------------------------------------------------------ */
lbx_result_t Meter_SetDiagTarget(meter_gauge_id_t id, u16 target)
{
    if (id >= METER_GAUGE_MAX) return LBX_ERR_PARAM;
    s_m.gauges[id].diag_hold = true;
    if (target > s_m.gauges[id].max) target = s_m.gauges[id].max;
    s_m.gauges[id].target = target;
    return LBX_OK;
}

u16 Meter_GetTarget(meter_gauge_id_t id)
{
    if (id >= METER_GAUGE_MAX) return 0u;
    return s_m.gauges[id].target;
}

u16 Meter_GetNow(meter_gauge_id_t id)
{
    if (id >= METER_GAUGE_MAX) return 0u;
    return s_m.gauges[id].now;
}

const mod_desc_t mod_meter = {
    .name      = "meter",
    .init      = prv_init,
    .on_ign_on = prv_on_ign_on,
    .tick      = prv_tick,
    .standby   = prv_standby,
};
