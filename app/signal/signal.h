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

lbx_result_t Signal_Set(signal_id_t id, int32_t value);
int32_t      Signal_Get(signal_id_t id);
bool         Signal_IsValid(signal_id_t id);
void         Signal_Invalidate(signal_id_t id);
void         Signal_InvalidateAll(void);

#endif /* LBX_SIGNAL_H */
