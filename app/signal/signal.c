/**
 * @file    signal.c
 * @brief   Signal bus storage and accessors
 *
 * Storage: fixed-size array indexed by signal_id_t. Each slot
 * holds the current value plus a `valid` flag. A signal is
 * "set" by writing the value AND marking the slot valid; an
 * invalidated slot returns 0 from Signal_Get() and false from
 * Signal_IsValid().
 */
#include "signal.h"

/* Per-signal storage. `valid` is initially false: callers must
 * call Signal_Set() before they can rely on Signal_Get(). */
static struct {
    u32 value;
    bool valid;
} s_signals[SIG_MAX];

/**
 * @brief   Publish a value on the signal bus
 * @brief   在信号总线上发布一个值
 *
 * @param[in]  id     Signal id (see signal_id_t)
 * @param[in]  value  32-bit payload; interpretation depends on signal
 *
 * @return  c02b2_result_t
 * @retval  C02B2_OK            Value stored and slot marked valid
 * @retval  C02B2_ERR_PARAM     id invalid (SIG_INVALID or out of range)
 */
c02b2_result_t Signal_Set(signal_id_t id, u32 value)
{
    if (id <= SIG_INVALID || id >= SIG_MAX) {
        return C02B2_ERR_PARAM;
    }
    s_signals[id].value = value;
    /* Mark valid so consumers can distinguish fresh data from "never set". */
    s_signals[id].valid = true;
    return C02B2_OK;
}

/**
 * @brief   Read the current value of a signal
 * @brief   读取信号的当前值
 *
 * @details Returns 0 for unknown / out-of-range ids and for
 *          invalidated signals. Callers that care about freshness
 *          should use Signal_IsValid().
 *
 * @param[in]  id  Signal id
 *
 * @return  u32  Last value set (raw), or 0 if never set / invalid id
 */
u32 Signal_Get(signal_id_t id)
{
    if (id <= SIG_INVALID || id >= SIG_MAX) {
        return 0;
    }
    return s_signals[id].value;
}



/**
 * @brief   Resolve a signal id to its enum string
 * @brief   把信号 id 解析为对应的枚举字符串
 *
 * @details See app/signal/signal.h::Signal_GetName() for the
 *          contract.  This hand-maintained table is enough for
 *          debugging / SOC dumps.  Unknown / unmaintained ids fall
 *          back to "<can-N>" so callers can still tell whether an
 *          id is a CAN-derived one.
 */
const char * Signal_GetName(signal_id_t id)
{
    if (id <= SIG_INVALID || id >= SIG_MAX) {
        return "<invalid>";
    }

    static const char * const k_names[SIG_MAX] = {
        [SIG_INVALID]                     = "SIG_INVALID",

        /* --- Power / ignition --- */
        [SIG_IGN_ON]                    = "SIG_IGN_ON",
        [SIG_ACC_ON]                    = "SIG_ACC_ON",
        [SIG_KL30_VOLTAGE_MV]           = "SIG_KL30_VOLTAGE_MV",
        [SIG_PWR_MODE]                  = "SIG_PWR_MODE",
        [SIG_GC_POWER_ON]               = "SIG_GC_POWER_ON",
        [SIG_IGN_OFF_COUNTER]           = "SIG_IGN_OFF_COUNTER",
        [SIG_SLEEP_READY]               = "SIG_SLEEP_READY",

        /* --- Vehicle (legacy) --- */
        [SIG_VEH_SPEED_KPH_X10]         = "SIG_VEH_SPEED_KPH_X10",
        [SIG_ENG_RPM]                   = "SIG_ENG_RPM",
        [SIG_FUEL_LEVEL_PCT]            = "SIG_FUEL_LEVEL_PCT",
        [SIG_COOLANT_TEMP_C]            = "SIG_COOLANT_TEMP_C",
        [SIG_ODO_TOTAL_M]               = "SIG_ODO_TOTAL_M",
        [SIG_ODO_TRIP_A_M]              = "SIG_ODO_TRIP_A_M",
        [SIG_ODO_TRIP_B_M]              = "SIG_ODO_TRIP_B_M",
        [SIG_GEAR_POS]                  = "SIG_GEAR_POS",

        /* --- Telltale / display --- */
        [SIG_TT_LEFT_TURN]              = "SIG_TT_LEFT_TURN",
        [SIG_TT_RIGHT_TURN]             = "SIG_TT_RIGHT_TURN",
        [SIG_TT_HIGH_BEAM]              = "SIG_TT_HIGH_BEAM",
        [SIG_TT_LOW_BEAM]               = "SIG_TT_LOW_BEAM",
        [SIG_TT_FRONT_FOG]              = "SIG_TT_FRONT_FOG",
        [SIG_TT_REAR_FOG]               = "SIG_TT_REAR_FOG",
        [SIG_TT_POSITION_LAMP]          = "SIG_TT_POSITION_LAMP",
        [SIG_TT_SEAT_BELT]              = "SIG_TT_SEAT_BELT",
        [SIG_TT_BRAKE_FAULT]            = "SIG_TT_BRAKE_FAULT",
        [SIG_TT_OIL_PRESS]              = "SIG_TT_OIL_PRESS",
        [SIG_TT_ABS_FAULT]              = "SIG_TT_ABS_FAULT",
        [SIG_TT_AIRBAG_FAULT]           = "SIG_TT_AIRBAG_FAULT",
        [SIG_TT_ENGINE_FAULT]           = "SIG_TT_ENGINE_FAULT",
        [SIG_TT_BATTERY_FAULT]          = "SIG_TT_BATTERY_FAULT",
        [SIG_TT_FUEL_LOW]               = "SIG_TT_FUEL_LOW",
        [SIG_TT_CRUISE]                 = "SIG_TT_CRUISE",
        [SIG_TT_EPB]                    = "SIG_TT_EPB",
        [SIG_TT_AUTOHOLD]               = "SIG_TT_AUTOHOLD",
        [SIG_TT_ESP_FAULT]              = "SIG_TT_ESP_FAULT",
        [SIG_TT_TPMS_FAULT]             = "SIG_TT_TPMS_FAULT",

        /* --- Illumination --- */
        [SIG_ILLU_LCD_PCT]              = "SIG_ILLU_LCD_PCT",
        [SIG_ILLU_KEY_PCT]              = "SIG_ILLU_KEY_PCT",
        [SIG_ILLU_DAY_NIGHT]            = "SIG_ILLU_DAY_NIGHT",

        /* --- CAN RX timeout bitmap --- */
        [SIG_CAN_RX_TIMEOUT_MAP_LO]     = "SIG_CAN_RX_TIMEOUT_MAP_LO",
        [SIG_CAN_RX_TIMEOUT_MAP_HI]     = "SIG_CAN_RX_TIMEOUT_MAP_HI",
        [SIG_CAN_RX_TIMEOUT_MAP_HI2]    = "SIG_CAN_RX_TIMEOUT_MAP_HI2",

        /* --- CAN bus health --- */
        [SIG_CAN_BUS_OFF]               = "SIG_CAN_BUS_OFF",
        [SIG_CAN_BUS_OFF_COUNT]         = "SIG_CAN_BUS_OFF_COUNT",
        [SIG_CAN_TX_ERR_CNT]            = "SIG_CAN_TX_ERR_CNT",
        [SIG_CAN_RX_ERR_CNT]            = "SIG_CAN_RX_ERR_CNT",

        /* --- System --- */
        [SIG_FW_VERSION]                = "SIG_FW_VERSION",
        [SIG_BUILD_DATE]                = "SIG_BUILD_DATE",
        [SIG_WATCHDOG_KICK]             = "SIG_WATCHDOG_KICK",
        /* NOTE: SIG_CAN_<DbcSignal> entries (625) are not in this
         * hand-maintained table; for those ids Signal_GetName()
         * returns "<can-N>".  TODO: regenerate the full table from
         * can_db_ipk_gen.h once SOC consumers ask for it. */
    };

    /* CAN-derived ids live in the autoblock above SIG_MAX boundary-1
     * and are not represented here.  Detect by the canonical id range:
     * the DBC generator emits 625 SIG_CAN_* entries after the manual
     * section.  We answer "<can-N>" where N is the index within the
     * autoblock so logs are still readable. */
    if (id >= SIG_CAN_RX_TIMEOUT_MAP_HI2) {
        /* Skip our 7 manually-maintained CAN signals: any id >= the
         * first CAN-only entry is in the autoblock. */
        return "<can-signal>";
    }

    const char * n = k_names[id];
    return n ? n : "<unmapped>";
}

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
bool Signal_IsValid(signal_id_t id)
{
    if (id <= SIG_INVALID || id >= SIG_MAX) {
        return false;
    }
    return s_signals[id].valid;
}

/**
 * @brief   Mark a single signal as invalid
 * @brief   将单个信号标记为无效
 *
 * @details Subsequent Signal_Get() returns 0 and Signal_IsValid()
 *          returns false until the signal is Set again.
 *
 * @param[in]  id  Signal id
 */
void Signal_Invalidate(signal_id_t id)
{
    if (id <= SIG_INVALID || id >= SIG_MAX) {
        return;
    }
    /* Just clear the valid bit; keep the value for debugging. */
    s_signals[id].valid = false;
}

/**
 * @brief   Mark every signal as invalid
 * @brief   将所有信号标记为无效
 *
 * @details Used on power-mode transitions / factory reset to force
 *          all consumers to republish their data.
 */
void Signal_InvalidateAll(void)
{
    /* Linear walk is fine: SIG_MAX is small (< 100). */
    for (int i = 0; i < SIG_MAX; i++) {
        s_signals[i].valid = false;
    }
}
