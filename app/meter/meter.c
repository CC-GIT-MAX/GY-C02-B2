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

/** @brief  Static per-gauge configuration (max steps, rise/fall speed). */
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

/**
 * @brief   Read raw physical quantity from the signal bus and scale to steps
 * @brief   从信号总线读取原始物理量并换算为步数
 *
 * @details Linear mapping for each gauge:
 *   - SPEED : (0.1 kph) * 240 / 2600  ->  240 steps covers 260 kph
 *   - RPM   : rpm * 80 / 8000        ->  80 steps covers 8000 rpm
 *   - FUEL  : pct * 54 / 100         ->  54 steps covers 100%
 *   - TEMP  : ((degC + 40) * 54) / 180  ->  -40..140 degC mapped to 0..54
 *
 *          Real projects would replace this with a calibration LUT
 *          (or piecewise interpolation) to match the gauge's dial.
 *
 * @param[in]  id  Gauge index
 *
 * @return  u16  Target in steps (clamped to gauge max)
 */
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
            /* SIG_FUEL_LEVEL_PCT: 0..100.  Scale: 54 steps / 100%. */
            int32_t raw = Signal_Get(SIG_FUEL_LEVEL_PCT);
            if (raw < 0)   raw = 0;
            if (raw > 100) raw = 100;
            return (u16)(((u32)raw * MTR_FUEL_MAX_STEPS) / 100u);
        }
        case METER_GAUGE_TEMP: {
            /* Map -40..140 degC to 0..54 steps (180 degC range). */
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

/**
 * @brief   Step `now` toward `target` at the configured rise/fall rate
 * @brief   以配置的上升/下降速率将 now 向 target 推进
 *
 * @details Asymmetric movement: typical automotive behavior is
 *          "snap up, ease down" - the rise speed is higher than
 *          the fall speed so the pointer reacts quickly to
 *          acceleration but settles smoothly during deceleration.
 *
 * @param[in]  id  Gauge index
 */
static void prv_drive_motor(meter_gauge_id_t id)
{
    gauge_state_t *g = &s_m.gauges[id];
    if (g->now < g->target) {
        /* Moving up: increment by rise_speed, but never overshoot. */
        u16 delta = g->target - g->now;
        g->now = (delta <= g->rise_speed) ? g->target : (g->now + g->rise_speed);
    } else if (g->now > g->target) {
        /* Moving down: decrement by fall_speed, but never undershoot. */
        u16 delta = g->now - g->target;
        g->now = (delta <= g->fall_speed) ? g->target : (g->now - g->fall_speed);
    }
    /* TODO: feed g->now to eTMR PWM driver for the gauge stepper. */
    (void)id;
}

/** @brief  Run a single gauge (read target + drive motor). */
static void prv_tick_one(meter_gauge_id_t id)
{
    gauge_state_t *g = &s_m.gauges[id];
    if (!g->diag_hold) {
        /* Sample the signal bus for a fresh target each tick. */
        g->target = prv_read_target(id);
        /* Clamp to the gauge's physical step range. */
        if (g->target > g->max) g->target = g->max;
    }
    /* Always drive the motor: smooth movement even in diag mode. */
    prv_drive_motor(id);
}

/* mod_desc_t hooks ----------------------------------------------------- */

/**
 * @brief   mod_desc_t init hook: zero all gauges, install per-gauge cfg.
 * @brief   mod_desc_t init 钩子：清零所有表头并安装配置
 *
 * @param[in]  cold_boot  1 = cold boot, 0 = warm boot
 */
static void prv_init(u8 cold_boot)
{
    (void)cold_boot;
    /* Per-gauge config copy from k_cfg[]. */
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

/**
 * @brief   mod_desc_t on_ign_on hook: re-zero all pointers (homing).
 * @brief   mod_desc_t on_ign_on 钩子：所有指针归零（回零）
 *
 * @details On every IGN cycle, pointers are commanded to 0 so
 *          they perform the homing sequence. The motor driver
 *          detects the home position sensor and latches.
 */
static void prv_on_ign_on(void)
{
    /* Re-zero pointers on IGN ON for safety (homing is board-specific). */
    for (u32 i = 0; i < METER_GAUGE_MAX; i++) {
        s_m.gauges[i].now    = 0u;
        s_m.gauges[i].target = 0u;
    }
    LOG_I("on_ign_on: pointers zeroed");
}

/**
 * @brief   mod_desc_t tick hook: update all four gauges @ 20 ms.
 * @brief   mod_desc_t tick 钩子：20ms 节拍更新 4 个表头
 */
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

/**
 * @brief   mod_desc_t standby hook: zero all targets before sleep.
 * @brief   mod_desc_t standby 钩子：休眠前将所有目标置零
 *
 * @details On standby, every gauge's target is set to 0 so the
 *          pointers will smoothly move to home before the MCU
 *          powers down. The motor driver may also disable the
 *          stepper coils here (board-specific).
 */
static void prv_standby(void)
{
    for (u32 i = 0; i < METER_GAUGE_MAX; i++) {
        s_m.gauges[i].target = 0u;
    }
    LOG_I("standby: targets zeroed");
}

/* Public API ------------------------------------------------------------ */

/**
 * @brief   Override a gauge target (diag mode)
 * @brief   覆盖某个表头的目标值（诊断模式）
 *
 * @details Sets the diag_hold flag so the signal-bus reader
 *          stops updating the target; the meter still moves
 *          smoothly to the new target via prv_drive_motor().
 *
 * @param[in]  id      Gauge index
 * @param[in]  target  Target steps (0..gauge max)
 *
 * @return  lbx_result_t
 * @retval  LBX_OK         Target accepted
 * @retval  LBX_ERR_PARAM  id out of range
 */
lbx_result_t Meter_SetDiagTarget(meter_gauge_id_t id, u16 target)
{
    if (id >= METER_GAUGE_MAX) return LBX_ERR_PARAM;
    /* diag_hold freezes the target source; movement remains smooth. */
    s_m.gauges[id].diag_hold = true;
    if (target > s_m.gauges[id].max) target = s_m.gauges[id].max;
    s_m.gauges[id].target = target;
    return LBX_OK;
}

/**
 * @brief   Get the current smoothed target
 * @brief   获取当前的平滑后目标值
 *
 * @param[in]  id  Gauge index
 *
 * @return  u16  Current target in steps (0 if id out of range)
 */
u16 Meter_GetTarget(meter_gauge_id_t id)
{
    if (id >= METER_GAUGE_MAX) return 0u;
    return s_m.gauges[id].target;
}

/**
 * @brief   Get the current pointer position
 * @brief   获取当前指针位置
 *
 * @param[in]  id  Gauge index
 *
 * @return  u16  Current position in steps (0 if id out of range)
 */
u16 Meter_GetNow(meter_gauge_id_t id)
{
    if (id >= METER_GAUGE_MAX) return 0u;
    return s_m.gauges[id].now;
}

/**
 * @brief   Module descriptor registered in scheduler.c
 * @brief   在 scheduler.c 中注册的模块描述符
 */
const mod_desc_t mod_meter = {
    .name      = "meter",
    .init      = prv_init,
    .on_ign_on = prv_on_ign_on,
    .tick      = prv_tick,
    .standby   = prv_standby,
};
