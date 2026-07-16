/**
 * @file    signal.c
 * @brief   信号总线存储与访问层（v0.5）
 *
 * 总线只保留每信号一个 RAW u32；validity 由 timeout bitmap 派生。
 * 模块内静态变量：
 *   - s_signals[]：u32[SIG_MAX]，仅数据，无 valid/ever_set 位；
 *     BSS 默认 0，冷启动由 can_rx.c::prv_mcu_init() 通过 Signal_Set()
 *     按 DBC init_value 预填。
 *   - s_boot_done：1 字节，prv_check_timeouts() 首次进入时置 1，
 *     prv_standby() (KL15 off) 清 0；用作 Signal_IsValid() 的
 *     bootstrap 窗口守卫（timeout monitor 跑过之前一律返 false）。
 *
 * 已删除的旧 API（v0.4 及之前）：
 *   - Signal_GetStored / Signal_Invalidate / Signal_InvalidateAll：
 *     timeout bitmap 已是 SoT，不再需要 per-slot 守卫。
 *   - Signal_Reset 语义变更：从"清三位"改为写 DBC init_value。
 *
 * 新增 API 见 signal.h：Signal_HasEverReceived / Signal_SetBootDone /
 * Signal_ResetBootDone / Signal_IsBootDone；validity 查询路径依赖
 * can_db.h::CanDb_SigToTimeoutBit()（反向查表，按需惰性构建）。
 *
 * 详见 ARCHITECTURE.md §4 + SIGNAL_GUIDE.md v0.5 段。
 */
#include "signal.h"
#include "can_db.h"   /* CanDb_SigToTimeoutBit() */

/* Per-signal RAW u32 value only -- no validity bookkeeping.
 * Boot defaults to 0 (BSS); can_rx.c::prv_mcu_init() seeds every
 * SIG_CAN_* slot with its DBC init_value via Signal_Set(). */
static u32 s_signals[SIG_MAX];

/* Bootstrap guard: 0 from BSS, set to 1 the first time
 * can_rx.c::prv_check_timeouts() runs, cleared by
 * can_rx.c::prv_standby() on KL15 off. */
static u8 s_boot_done;

/* ---------------------------------------------------------------- *
 *  Init / Reset / Boot done                                         *
 * ---------------------------------------------------------------- */

/**
 * @brief   标记 bootstrap 窗口为已完成（timeout monitor 已至少跑过一次）。
 */
void Signal_SetBootDone(void)
{
    s_boot_done = 1u;
}

/**
 * @brief   清除 bootstrap 标志（KL15 off / standby 入口）。
 */
void Signal_ResetBootDone(void)
{
    s_boot_done = 0u;
}

/**
 * @brief   读取当前 bootstrap 标志状态。
 *
 * @return  true=timeout monitor 已跑过；false=仍在启动期窗口内
 */
bool Signal_IsBootDone(void)
{
    return s_boot_done != 0u;
}

/* ---------------------------------------------------------------- *
 *  Set / Get                                                        *
 * ---------------------------------------------------------------- */

/**
 * @brief   将 RAW u32 写入信号总线指定槽位。
 *
 * @param[in]  id     信号 id（SIG_INVALID/SIG_MAX 之外）
 * @param[in]  value  要写入的原始 u32 值
 *
 * @return  C02B2_OK 或 C02B2_ERR_PARAM（id 越界）
 */
c02b2_result_t Signal_Set(signal_id_t id, u32 value)
{
    if (id <= SIG_INVALID || id >= SIG_MAX) {
        return C02B2_ERR_PARAM;
    }
    s_signals[id] = value;
    return C02B2_OK;
}

/**
 * @brief   读取指定槽位当前的 RAW u32 值（无 valid 守卫）。
 *
 * @param[in]  id  信号 id
 *
 * @return  槽位当前值（越界返 0）
 */
u32 Signal_Get(signal_id_t id)
{
    if (id <= SIG_INVALID || id >= SIG_MAX) {
        return 0u;
    }
    return s_signals[id];
}

/**
 * @brief   写入 DBC init_value 进槽位（冷启动 helper）。
 *
 * @details 仅 prv_mcu_init() 在每个 SIG_CAN_* 上调用一次，
 *          让上电后未收到帧时上层读到的就是 DBC 默认值
 *          （例如 TPMS_FLTyrePr = 0xFF 表示 Invalid 标识）。
 *          越界 id 静默忽略。
 *
 * @param[in]  id  信号 id
 */
void Signal_Reset(signal_id_t id)
{
    if (id <= SIG_INVALID || id >= SIG_MAX) {
        return;
    }
    s_signals[id] = can_sig_descs_ipk[(u16)(id - 1u)].init_value;
}

/**
 * @brief   查询指定信号是否处于"当前可信任"状态。
 *
 * @details v0.5 模型：由 timeout bitmap 派生。
 *   - bootstrap 窗口（boot_done==0）：一律 false；
 *   - 启动后：true 当且仅当 id 所属 MSG 的 timeout bit == 0；
 *   - 不在 timeout map 覆盖范围（CAN bus health 信号）：
 *     兜底为 value != 0 即 true。
 *
 * @param[in]  id  信号 id
 *
 * @return  true=当前 valid；false=启动期/MSG 超时/id 越界
 */
bool Signal_IsValid(signal_id_t id)
{
    if (id <= SIG_INVALID || id >= SIG_MAX) {
        return false;
    }
    if (s_boot_done == 0u) {
        /* Bootstrap window: timeout monitor has not run yet, treat
         * every signal as invalid so consumers do not act on stale
         * BSS zeros. */
        return false;
    }
    const u8 bit = CanDb_SigToTimeoutBit(id);
    if (bit >= CAN_BITMAP_MAX) {
        /* Signal not covered by the timeout bitmap (e.g. CAN bus
         * health signals SIG_CAN_BUS_OFF etc.). These have no
         * timeout semantics -- fall back to "valid iff value != 0". */
        return s_signals[id] != 0u;
    }
    const u32 map = (bit < 32u)  ? Signal_Get(SIG_CAN_RX_TIMEOUT_MAP_LO)
                  : (bit < 64u)  ? Signal_Get(SIG_CAN_RX_TIMEOUT_MAP_HI)
                  :                Signal_Get(SIG_CAN_RX_TIMEOUT_MAP_HI2);
    return (map & ((u32)1u << (bit & 31u))) == 0u;
}

/**
 * @brief   查询指定信号所属 MSG 自 ign_on 以来是否曾收到过有效帧。
 *
 * @details 与 Signal_IsValid 的区别：timeout bit 会因超时来回切换；
 *          ever_received bit 仅在 prv_drain() 收到帧时置 1，
 *          由 prv_standby() 清 0（KL15 off）。
 *          适用：仪表降级显示、超时前最后一帧兜底。
 *
 * @param[in]  id  信号 id
 *
 * @return  true=本轮 ign_on 后曾收到过；false=从未收到/越界
 */
bool Signal_HasEverReceived(signal_id_t id)
{
    if (id <= SIG_INVALID || id >= SIG_MAX) {
        return false;
    }
    const u8 bit = CanDb_SigToTimeoutBit(id);
    if (bit >= CAN_BITMAP_MAX) {
        return false;
    }
    const u32 ever = (bit < 32u)  ? Signal_Get(SIG_CAN_RX_EVER_RECEIVED_LO)
                    : (bit < 64u)  ? Signal_Get(SIG_CAN_RX_EVER_RECEIVED_HI)
                    :                Signal_Get(SIG_CAN_RX_EVER_RECEIVED_HI2);
    return (ever & ((u32)1u << (bit & 31u))) != 0u;
}

/**
 * @brief   查询指定信号 id 的可读名称（log 用）。
 *
 * @details 手维护名称表覆盖 SIG_INVALID + 3 个 timeout bitmap +
 *          3 个 ever_received bitmap + 4 个 bus health。
 *          其余 id（DBC 自动生成的 SIG_CAN_*）返"<can-signal>"占位。
 *
 * @param[in]  id  信号 id
 *
 * @return  永不返 NULL；越界返"<invalid>"，未映射返"<unmapped>"
 */
const char * Signal_GetName(signal_id_t id)
{
    if (id <= SIG_INVALID || id >= SIG_MAX) {
        return "<invalid>";
    }
    static const char * const k_names[SIG_MAX] = {
        [SIG_INVALID]                     = "SIG_INVALID",

        /* --- CAN RX timeout bitmap --- */
        [SIG_CAN_RX_TIMEOUT_MAP_LO]       = "SIG_CAN_RX_TIMEOUT_MAP_LO",
        [SIG_CAN_RX_TIMEOUT_MAP_HI]       = "SIG_CAN_RX_TIMEOUT_MAP_HI",
        [SIG_CAN_RX_TIMEOUT_MAP_HI2]      = "SIG_CAN_RX_TIMEOUT_MAP_HI2",

        /* --- CAN RX ever-received bitmap --- */
        [SIG_CAN_RX_EVER_RECEIVED_LO]     = "SIG_CAN_RX_EVER_RECEIVED_LO",
        [SIG_CAN_RX_EVER_RECEIVED_HI]     = "SIG_CAN_RX_EVER_RECEIVED_HI",
        [SIG_CAN_RX_EVER_RECEIVED_HI2]    = "SIG_CAN_RX_EVER_RECEIVED_HI2",

        /* --- CAN bus health --- */
        [SIG_CAN_BUS_OFF]                 = "SIG_CAN_BUS_OFF",
        [SIG_CAN_BUS_OFF_COUNT]           = "SIG_CAN_BUS_OFF_COUNT",
        [SIG_CAN_TX_ERR_CNT]              = "SIG_CAN_TX_ERR_CNT",
        [SIG_CAN_RX_ERR_CNT]              = "SIG_CAN_RX_ERR_CNT",
    };
    /* SIG_CAN_RX_TIMEOUT_MAP_HI2 = 3, SIG_CAN_RX_EVER_RECEIVED_HI2 = 6,
     * SIG_CAN_RX_ERR_CNT = 8 (last hand-named ID). Above that,
     * ids are autogenerated by tools/dbc_parse.py (SIG_CAN_IPK_...,
     * SIG_CAN_EMS_... etc.). Those get a stable placeholder so log
     * readers can still distinguish "CAN signal id" from
     * "special map signal id". */
    if (id > (signal_id_t)SIG_CAN_RX_ERR_CNT) {
        return "<can-signal>";
    }
    const char * n = k_names[id];
    return n ? n : "<unmapped>";
}
