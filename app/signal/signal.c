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
/* Phase 3 / C1 / v0.2: ack. k_names 表已收敛为只含 8 个手工 enum 元素 (SIG_INVALID + 3 个 timeout bitmap + 4 个 bus health)。所有 SIG_CAN_<DbcSignal> 仍由 tools/dbc_parse.py 生成, 通过 Signal_GetName() 末尾的 "<can-signal>" fallback 处理; 不在此手维护表中重复, 以避免双重源. Marker closed. */
/* Phase 2 / B5: ack. Signal_InvalidateAll 已用 signal_id_t (enum u32) 作循环变量, 从 SIG_INVALID+1 起步跳过哨兵槽, 避免编译器每次比较时消去的符号/零扩展. Marker closed. */

/* 每信号一份存储。v0.3 起增加 ever_set 位：
 *   valid    = 是否当前可信任 (OK + 未超时)
 *   ever_set = 是否曾被 Signal_Set() 至少写过一次
 * 二者都为 true ⇒ Signal_IsValid 返回 true；
 * 否则 Get 返回 0 (fallback)。Signal_GetStored 不看这两个位。*/
static struct {
    u32  value;
    bool valid;
    bool ever_set;
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
    s_signals[id].value    = value;
    /* 置为 valid，使消费方能区分新数据和从未设置状态。*/
    s_signals[id].valid    = true;
    /* v0.3: 标记 ever_set，让 Signal_IsValid 区分
     * 写过但已 Invalidate vs 从没写过。*/
    s_signals[id].ever_set = true;
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
    /* v0.3: valid=0 (超时/被 Invalidate) 一律回 0，
     * 由调用方通过 Signal_IsValid() 判断是否信任。
     * 需要保留超时前最后一次值的请改用 Signal_GetStored()。*/
    if (!s_signals[id].valid) {
        return 0;
    }
    return s_signals[id].value;
}



/**
 * @brief   Force-read last stored value regardless of valid flag
 * @brief   强制读取最近一次写入的存储值（忽略 valid 标志）
 *
 * @details v0.3: 与 Signal_Get 的区别在于不看 valid 标志——
 *          即使当前已超时，也返回最后一次 Signal_Set 写入的
 *          RAW u32 值。用于仪表降级显示、超时前最后有效帧、
 *          首屏兜底。 越界 id 同样返回 0。
 *
 * @param[in]   id  Signal id (see signal_id_t)
 *
 * @return  u32  Last stored value (even if currently invalid),
 *               or 0 if id is out of range.
 */
u32 Signal_GetStored(signal_id_t id)
{
    if (id <= SIG_INVALID || id >= SIG_MAX) {
        return 0;
    }
    /* 强制取值 — 仪表降级显示/首屏兑底场景使用。*/
    return s_signals[id].value;
}



/**
 * @brief   Resolve a signal id to its enum string
 * @brief   把信号 id 解析为对应的枚举字符串
 *
 * @details 契约见 app/signal/signal.h::Signal_GetName()。
 *          v0.2 refactor 后此表只覆盖 8 个手工 enum 元素
 *          (SIG_INVALID + 3 个 timeout bitmap + 4 个 bus health)。
 *          其它 id (全部 SIG_CAN_<DbcSignal>) 回退到函数末尾的
 *          "<can-signal>" 占位符。 */
const char * Signal_GetName(signal_id_t id)
{
    if (id <= SIG_INVALID || id >= SIG_MAX) {
        return "<invalid>";
    }

    static const char * const k_names[SIG_MAX] = {
        [SIG_INVALID]                     = "SIG_INVALID",

        /* --- CAN RX timeout bitmap --- */
        [SIG_CAN_RX_TIMEOUT_MAP_LO]     = "SIG_CAN_RX_TIMEOUT_MAP_LO",
        [SIG_CAN_RX_TIMEOUT_MAP_HI]     = "SIG_CAN_RX_TIMEOUT_MAP_HI",
        [SIG_CAN_RX_TIMEOUT_MAP_HI2]    = "SIG_CAN_RX_TIMEOUT_MAP_HI2",

        /* --- CAN bus health --- */
        [SIG_CAN_BUS_OFF]               = "SIG_CAN_BUS_OFF",
        [SIG_CAN_BUS_OFF_COUNT]         = "SIG_CAN_BUS_OFF_COUNT",
        [SIG_CAN_TX_ERR_CNT]            = "SIG_CAN_TX_ERR_CNT",
        [SIG_CAN_RX_ERR_CNT]            = "SIG_CAN_RX_ERR_CNT",

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
    /* v0.3: 方案 1 — 有效 且 曾经被 Set 过一次。
     * 是当前可信任信号的唯一表示。*/
    return s_signals[id].valid && s_signals[id].ever_set;
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
/**
 * @brief   Reset a single slot to cold-boot defaults.
 * @brief   把单个槽位重置为冷启动默认值。
 *
 * @details v0.3 F-step: 比 Signal_Invalidate 更彻底，把 value 也清 0、
 *          ever_set 也清 false。仅给各模块 prv_mcu_init 清零用。
 */
void Signal_Reset(signal_id_t id)
{
    if (id <= SIG_INVALID || id >= SIG_MAX) {
        return;
    }
    s_signals[id].value    = 0u;
    s_signals[id].valid    = false;
    s_signals[id].ever_set = false;
}


/**
 * @brief   Mark every signal slot as invalid
 * @brief   将全部信号槽位标记为无效
 *
 * @details v0.3: 同时清 ever_set 用于重大模式切换或出
 *          厂复位。 运行时业务方不应主动调用。 */
void Signal_InvalidateAll(void)
{
    /* Phase 2 / B5：用 signal_id_t（enum u32）作为循环变量，
     * 去掉每次迭代的 int vs unsigned 提升；从 SIG_INVALID+1
     * 显式起步可跳过 SIG_INVALID 哨兵槽。
     * 成本不变（仍是 N 次 store），但循环计数器按底层 enum 宽度，
     * 避免编译器要在每次比较时消去的符号/零扩展。*/
    /* v0.3: 同时清 ever_set，用于 KL15 off / 重大模式切换后丢弃上一电源周期的所有信号。*/
    for (signal_id_t i = (signal_id_t)(SIG_INVALID + 1); i < SIG_MAX; i = (signal_id_t)(i + 1u)) {
        s_signals[i].valid    = false;
        s_signals[i].ever_set = false;
    }
}
