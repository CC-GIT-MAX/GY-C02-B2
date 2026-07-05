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

#include "types.h"
#include "result.h"

typedef enum {
    SIG_INVALID = 0,

    /* ---------------------------------------------------------------- *
     *  Power / ignition                                                *
     * ---------------------------------------------------------------- */
    SIG_IGN_ON,            /* bool, 1 = KL15 on                       */
    SIG_ACC_ON,            /* bool, 1 = ACC on                        */
    SIG_KL30_VOLTAGE_MV,   /* int32, KL30 voltage in mV               */
    SIG_PWR_MODE,          /* int32, see pwr_mode_t                   */
    SIG_GC_POWER_ON,       /* bool                                    */
    SIG_IGN_OFF_COUNTER,   /* int32, ticks since IGN off              */
    SIG_SLEEP_READY,       /* bool, all modules allow sleep           */

    /* ---------------------------------------------------------------- *
     *  Vehicle                                                         *
     * ---------------------------------------------------------------- */
    SIG_VEH_SPEED_KPH_X10, /* int32, 0.1 kph                          */
    SIG_ENG_RPM,           /* int32, rpm                              */
    SIG_FUEL_LEVEL_PCT,    /* int32, 0..100                           */
    SIG_COOLANT_TEMP_C,    /* int32, degC                             */
    SIG_ODO_TOTAL_M,       /* int32, total odometer in meters         */
    SIG_ODO_TRIP_A_M,      /* int32, trip A in meters                 */
    SIG_ODO_TRIP_B_M,      /* int32, trip B in meters                 */
    SIG_GEAR_POS,          /* int32, see gear_t                       */

    /* ---------------------------------------------------------------- *
     *  Telltale / display                                              *
     * ---------------------------------------------------------------- */
    SIG_TT_LEFT_TURN,      /* bool                                    */
    SIG_TT_RIGHT_TURN,     /* bool                                    */
    SIG_TT_HIGH_BEAM,      /* bool                                    */
    SIG_TT_LOW_BEAM,       /* bool                                    */
    SIG_TT_FRONT_FOG,      /* bool                                    */
    SIG_TT_REAR_FOG,       /* bool                                    */
    SIG_TT_POSITION_LAMP,  /* bool                                    */
    SIG_TT_SEAT_BELT,      /* bool                                    */
    SIG_TT_BRAKE_FAULT,    /* bool                                    */
    SIG_TT_OIL_PRESS,      /* bool                                    */
    SIG_TT_ABS_FAULT,      /* bool                                    */
    SIG_TT_AIRBAG_FAULT,   /* bool                                    */
    SIG_TT_ENGINE_FAULT,   /* bool                                    */
    SIG_TT_BATTERY_FAULT,  /* bool                                    */
    SIG_TT_FUEL_LOW,       /* bool                                    */
    SIG_TT_CRUISE,         /* bool                                    */
    SIG_TT_EPB,            /* bool                                    */
    SIG_TT_AUTOHOLD,       /* bool                                    */
    SIG_TT_ESP_FAULT,      /* bool                                    */
    SIG_TT_TPMS_FAULT,     /* bool                                    */
    /* ... append new telltale signals here ... */

    /* ---------------------------------------------------------------- *
     *  Illumination                                                    *
     * ---------------------------------------------------------------- */
    SIG_ILLU_LCD_PCT,      /* int32, 0..100, LCD backlight            */
    SIG_ILLU_KEY_PCT,      /* int32, 0..100, key backlight            */
    SIG_ILLU_DAY_NIGHT,    /* bool, 1 = night                         */

    /* ---------------------------------------------------------------- *
     *  CAN receive status (timeout flags)                              *
     * ---------------------------------------------------------------- */
    SIG_CAN_RX_TIMEOUT_0,  /* bool, ID0 timeout                       */
    SIG_CAN_RX_TIMEOUT_1,  /* bool, ID1 timeout                       */
    /* ... append as needed, or use a bitmap signal below ...          */
    SIG_CAN_RX_TIMEOUT_MAP,/* int32, bitfield of timeout flags         */

    /* ---------------------------------------------------------------- *
     *  System                                                          *
     * ---------------------------------------------------------------- */
    SIG_FW_VERSION,        /* int32, packed MAJOR/MINOR/PATCH          */
    SIG_BUILD_DATE,        /* int32, packed YYYYMMDD                  */
    SIG_WATCHDOG_KICK,     /* bool, debug: kicked this tick            */

    SIG_MAX
} signal_id_t;

/**
 * @brief   Publish a value on the signal bus
 * @brief   在信号总线上发布一个值
 *
 * @details Stamps the value and marks the slot as valid. The
 *          owner of the signal is responsible for the rate.
 *          Returns LBX_ERR_PARAM for SIG_INVALID / out-of-range ids.
 *
 * @param[in]  id     Signal id (see signal_id_t)
 * @param[in]  value  32-bit payload; interpretation depends on signal
 *
 * @return  lbx_result_t
 * @retval  LBX_OK            Value stored
 * @retval  LBX_ERR_PARAM     id invalid
 */
lbx_result_t Signal_Set(signal_id_t id, int32_t value);

/**
 * @brief   Read the current value of a signal
 * @brief   读取信号的当前值
 *
 * @details Returns 0 for unknown / out-of-range ids. Callers
 *          that care about freshness should use Signal_IsValid().
 *
 * @param[in]  id  Signal id
 *
 * @return  int32_t  Last value set, or 0 if never set / invalid id
 */
int32_t      Signal_Get(signal_id_t id);

/**
 * @brief   Check whether the signal slot is currently valid
 * @brief   检查信号槽位当前是否有效
 *
 * @param[in]  id  Signal id
 *
 * @return  bool
 * @retval  true   Signal has been set and not invalidated
 * @retval  false  Never set, explicitly invalidated, or invalid id
 */
bool         Signal_IsValid(signal_id_t id);

/**
 * @brief   Mark a single signal as invalid (next Get returns 0)
 * @brief   将单个信号标记为无效（下次 Get 返回 0）
 *
 * @param[in]  id  Signal id
 */
void         Signal_Invalidate(signal_id_t id);

/**
 * @brief   Mark every signal as invalid
 * @brief   将所有信号标记为无效
 *
 * @details Used on power-mode transitions / factory reset to force
 *          all consumers to republish.
 */
void         Signal_InvalidateAll(void);

#endif /* LBX_SIGNAL_H */
