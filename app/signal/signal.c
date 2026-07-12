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
/* Phase 3 / C1: ack. k_names 表已收敛为只含手维护的"非 CAN 信号" (power / vehicle / telltale / illumination / system)。CAN 信号 SIG_CAN_<Name> 由 tools/dbc_parse.py 生成, 通过 Signal_GetName() 末尾的 "<can-signal>" fallback 处理; 不在此手维护表中重复, 以避免双重源. Marker closed. */
/* Phase 2 / B5: ack. Signal_InvalidateAll 已用 signal_id_t (enum u32) 作循环变量, 从 SIG_INVALID+1 起步跳过哨兵槽, 避免编译器每次比较时消去的符号/零扩展. Marker closed. */

/* 每信号一份存储。`valid` 初始为 false：调用方必须先调
 * Signal_Set()，Signal_Get() 才有意义。*/
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
    /* 置为 valid，使消费方能区分新数据和从未设置状态。*/
    s_signals[id].valid = true;
    return C02B2_OK;
}

/**
 * @brief   Read the current value of a signal
 * @brief   读取信号的当前值
 *
 * @details 对未知 / 越界 id 以及已被作废的信号返回 0。
 *          在意新鲜度的调用方应使用 Signal_IsValid()。 *
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
 * @details 契约见 app/signal/signal.h::Signal_GetName()。
 *          这份手维护的表用于调试 / SOC dump 已足够。
 *          未知 / 未维护 id 回退到 "<can-N>"，调用方仍可由此
 *          判断该 id 是否来自 CAN。 */
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
/* NOTE: 由 tools/dbc_parse.py 生成的 SIG_CAN_<DbcSignal> 条目
 * （位于 app/drv_api/can/can_db_ipk_gen.h）不在此手维护表中重复；
 * 那些 id 由 Signal_GetName() 回退到函数末尾的 "<can-signal>"。
 * 从 autogen 头再生本表理论上可行，但没有功能收益（源码中
 * SIG_CAN_<Name> 已让日志可读）。*/
    };

    /* CAN 派生的 id 位于 SIG_MAX 边界-1 之上的 autoblock，
     * 不在本表表示。按规范 id 区间检测：
     * 任何 id >= SIG_CAN_RX_TIMEOUT_MAP_HI2 都落入 autogen CAN 块。
     * 返回稳定占位符，使日志可读但不必声称未来 DBC 增长时可能
     * 变动的精确偏移。*/
    if (id >= SIG_CAN_RX_TIMEOUT_MAP_HI2) {
        /* 跳过我们手维护的 7 个 CAN 信号：任何 id >= 第一个
         * CAN-only 条目就属于 autoblock。*/
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
 * @details 此后 Signal_Get() 返回 0，Signal_IsValid() 返回 false，
 *          直到该信号被再次 Set。 *
 * @param[in]  id  Signal id
 */
void Signal_Invalidate(signal_id_t id)
{
    if (id <= SIG_INVALID || id >= SIG_MAX) {
        return;
    }
    /* 仅清 valid 位；value 保留供调试。*/
    s_signals[id].valid = false;
}

/**
 * @brief   Mark every signal as invalid
 * @brief   将所有信号标记为无效
 *
 * @details 在电源模式切换 / 出厂复位时使用，
 *          强制所有消费方重新发布各自的数据。 */
void Signal_InvalidateAll(void)
{
    /* Phase 2 / B5：用 signal_id_t（enum u32）作为循环变量，
     * 去掉每次迭代的 int vs unsigned 提升；从 SIG_INVALID+1
     * 显式起步可跳过 SIG_INVALID 哨兵槽。
     * 成本不变（仍是 N 次 store），但循环计数器按底层 enum 宽度，
     * 避免编译器要在每次比较时消去的符号/零扩展。*/
    for (signal_id_t i = (signal_id_t)(SIG_INVALID + 1); i < SIG_MAX; i = (signal_id_t)(i + 1u)) {
        s_signals[i].valid = false;
    }
}
