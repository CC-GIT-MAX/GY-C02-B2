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
/**
 * @brief   Per-gauge state (steps in motor, target, smoothing config)
 * @brief   单个表头状态（电机当前位置、目标、平滑参数）
 */
typedef struct {
    u16 now;       /* current steps in motor                    */
    u16 target;    /* desired steps (after smoothing)           */
    u16 max;       /* physical steps, e.g. 240 for speedo       */
    u8  speed;     /* movement speed (steps per tick)           */
} meter_gauge_t;

/**
 * @brief   Logical gauge identifiers
 * @brief   逻辑表头编号
 */
typedef enum {
    METER_GAUGE_SPEED = 0,
    METER_GAUGE_RPM,
    METER_GAUGE_FUEL,
    METER_GAUGE_TEMP,
    METER_GAUGE_MAX,
} meter_gauge_id_t;

/* Test/diag API */
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
lbx_result_t Meter_SetDiagTarget(meter_gauge_id_t id, u16 target);

/**
 * @brief   Get the current smoothed target
 * @brief   获取当前的平滑后目标值
 *
 * @param[in]  id  Gauge index
 *
 * @return  u16  Current target in steps (0 if id out of range)
 */
u16 Meter_GetTarget(meter_gauge_id_t id);

/**
 * @brief   Get the current pointer position
 * @brief   获取当前指针位置
 *
 * @param[in]  id  Gauge index
 *
 * @return  u16  Current position in steps (0 if id out of range)
 */
u16 Meter_GetNow(meter_gauge_id_t id);

/* Module descriptor */
/**
 * @brief   Module descriptor for mod_meter (registered in scheduler.c)
 * @brief   mod_meter 的模块描述符（在 scheduler.c 中注册）
 */
extern const mod_desc_t mod_meter;

#ifdef __cplusplus
}
#endif

#endif /* LBX_MOD_METER_H */
