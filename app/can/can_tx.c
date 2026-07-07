/**
 * @file    can_tx.c
 * @brief   Cyclic + event-triggered CAN transmit (DBC-driven)
 * @brief   周期 + 事件驱动的 CAN 发送（DBC 驱动）
 *
 * Walks `can_msg_descs_ipk[]` once per 10 ms tick.  For each TX
 * message:
 *   - If cycle_ms > 0, sends when the period has elapsed.
 *   - If cycle_ms == 0, sends only when armed by CanTx_Trigger().
 *
 * The payload itself is **NOT** built here -- business modules must
 * call `CanDb_EncodeAndPack()` (or `CanDb_DispatchByDb()` on RX)
 * before the tick fires, into a per-message scratch buffer that
 * the TX module then sends.  See `app/can/can_tx.h` for the
 * scratch-buffer API.
 */
#include "can_tx.h"
#include "drv_api/can/can_if.h"
#include "drv_api/can/can_db.h"
#include "rti.h"
#include "signal.h"

#define LOG_NAME  "CTX "
#include "log.h"

#define MAX_TX_TRACKED  32u

typedef struct {
    u32 last_send_tick_ms;   /**< RTI tick at last successful tx (0 = never) */
    u8  pending;             /**< set by CanTx_Trigger, cleared after send   */
} tx_track_t;

/**
 * @brief   Per-TX-message scratch buffer: business modules fill
 *          `data` and `dlc` via CanTx_PreparePayload() before the
 *          next 10 ms tick.
 * @brief   每条 TX 报文的 scratch 缓冲: 业务模块在下一个 10 ms tick
 *          前通过 CanTx_PreparePayload() 填好 data 和 dlc
 *
 * Index = can_msg_descs_ipk[].is_tx messages, in the same order.
 */
typedef struct {
    u8  data[8];
    u8  dlc;
    u16 cycle_ms;            /**< copy of the message's send period    */
    u16 _pad;
} tx_payload_t;

static struct {
    bool          init_done;
    tx_track_t    track[MAX_TX_TRACKED];
    tx_payload_t  payloads[MAX_TX_TRACKED];
    u16           tx_msg_idx[MAX_TX_TRACKED];   /**< map slot -> ipk index */
    u16           tx_count;                      /**< number of TX entries  */
} s_tx;
/* ---------------------------------------------------------------- *
 *  TX cyclic period table                                            *
 *                                                                    *
 *  Per-IPK-TX-message cyclic send period.  Read by prv_build_tx_table *
 *  during mcu_init to seed s_tx.payloads[].cycle_ms.  Override at    *
 *  runtime via CanTx_SetCycle(). 0 = event-driven only (no cyclic).  *
 * ---------------------------------------------------------------- */

/**
 * @brief   Static default cyclic period for every IPK TX message
 * @brief   每条 IPK TX 报文的静态默认发送周期
 *
 * @details Index matches `can_msg_descs_ipk[]`. Entries for RX
 *          messages are placeholders (0) and never read because
 *          prv_build_tx_table only walks `is_tx==1` rows.
 *
 *          Adjust values here, or override at runtime via
 *          CanTx_SetCycle(can_id, cycle_ms). Units: milliseconds.
 */
static const u16 g_can_tx_cycle_table[CAN_DB_IPK_MSG_COUNT] = {
    /* 0  MMI_DateTime_Msg         (RX) */ 0u,
    /* 1  MMI_GPS_Info5            (RX) */ 0u,
    /* 2  MMI_Status_Info          (RX) */ 0u,
    /* 3  MMI_Safety_Info          (RX) */ 0u,
    /* 4  MMI_SOCSet               (RX) */ 0u,
    /* 5  EMS_EngRelateTrqSts      (RX) */ 0u,
    /* 6  EMS_EngineRPM            (RX) */ 0u,
    /* 7  EMS_EngineDriverInfo     (RX) */ 0u,
    /* 8  EMS_EnginePatsBatteryStat(RX) */ 0u,
    /* 9  EMS_OBD_Info             (RX) */ 0u,
    /* 10 IPK_EngineService        (TX) */ 1000u,  /* 1 s  */
    /* 11 IPK_STS                  (TX) */ 100u,   /* 100 ms (mod_can_demo uses 200ms) */
    /* 12 IPK_SettingRequest       (TX) */ 100u,   /* 100 ms (mod_can_demo uses 200ms) */
};

/**
 * @brief   Locate the slot index in s_tx for a given IPK TX can_id.
 * @brief   按 IPK TX can_id 查找 s_tx 中的 slot 索引
 *
 * @param[in]  can_id  CAN id of an IPK TX message
 *
 * @return  u16  Slot index, or 0xFFFFu if not found
 */
static u16 prv_find_slot(u32 can_id)
{
    for (u16 slot = 0; slot < s_tx.tx_count; slot++) {
        const u16 msg_idx = s_tx.tx_msg_idx[slot];
        if (can_msg_descs_ipk[msg_idx].can_id == can_id) {
            return slot;
        }
    }
    return 0xFFFFu;
}

/**
 * @brief   One-shot init: scan the IPK message table for TX entries
 *          and build the slot-to-ipk-index map.
 * @brief   一次性初始化: 扫描 IPK 报文表中的 TX 条目, 建立 slot 到
 *          ipk 索引的映射
 */
static void prv_build_tx_table(void)
{
    s_tx.tx_count = 0u;
    for (u16 i = 0; i < (u16)CAN_DB_IPK_MSG_COUNT; i++) {
        if (!can_msg_descs_ipk[i].is_tx) continue;
        if (s_tx.tx_count >= MAX_TX_TRACKED) {
            LOG_W("TX table full, message %u skipped", (unsigned)i);
            break;
        }
        const u16 slot = s_tx.tx_count;
        s_tx.tx_msg_idx[slot] = i;
        s_tx.payloads[slot].dlc = can_msg_descs_ipk[i].dlc;
        /* Default cycle from g_can_tx_cycle_table[]. Override per-msg
         * at runtime via CanTx_SetCycle(). */
        s_tx.payloads[slot].cycle_ms = g_can_tx_cycle_table[i];
        for (u8 b = 0; b < 8u; b++) { s_tx.payloads[slot].data[b] = 0u; }
        s_tx.tx_count++;
    }
}

/**
 * @brief   Send the payload currently in slot `slot` immediately.
 * @brief   立即发送当前在 slot 槽位中的 payload
 */
static void prv_send(u16 slot)
{
    if (slot >= s_tx.tx_count) return;
    const u16 msg_idx = s_tx.tx_msg_idx[slot];
    const can_msg_desc_t *d = &can_msg_descs_ipk[msg_idx];

    can_msg_t m = {0};
    m.id  = d->can_id;
    m.ide = 0u;   /* IPK DBC uses standard 11-bit ids */
    m.dlc = s_tx.payloads[slot].dlc;
    for (u8 i = 0; i < m.dlc && i < 8u; i++) {
        m.data[i] = s_tx.payloads[slot].data[i];
    }

    if (CanIf_Send(CAN_CH_PUBLIC, &m) == C02B2_OK) {
        s_tx.track[slot].last_send_tick_ms = RTI_GetTick1ms();
        s_tx.track[slot].pending = 0u;
    }
}

/* ---------------------------------------------------------------- *
 *  Module lifecycle                                                 *
 * ---------------------------------------------------------------- */

/**
 * @brief   mod_desc_t mcu_init hook: zero state and build TX slot map.
 * @brief   mod_desc_t mcu_init 钩子: 清零状态并构建 TX slot 映射
 */
static void prv_mcu_init(u8 cold_boot)
{
    (void)cold_boot;
    for (u32 i = 0; i < MAX_TX_TRACKED; i++) {
        s_tx.track[i].last_send_tick_ms = 0u;
        s_tx.track[i].pending = 0u;
    }
    prv_build_tx_table();
    s_tx.init_done = true;
    LOG_I("init (cold=%u, tx=%u)", (unsigned)cold_boot, (unsigned)s_tx.tx_count);
}
/**
 * @brief   mod_desc_t wakeup_init hook: post-MCU-init restore.
 * @brief   mod_desc_t wakeup_init 钩子: MCU 初始化后的唤醒恢复
 *
 * @details Runs after mcu_init() and before on_ign_on(). Use this
 *          hook to re-arm NVIC priorities, restore wake-source
 *          state, or prime caches that mcu_init left in a known
 *          reset configuration. Currently a stub for all modules
 *          - extend when a module needs real wake-from-reset work.
 */
static void prv_wakeup_init(void)
{
    LOG_I("wakeup_init");
}

/**
 * @brief   mod_desc_t on_ign_on hook: restart cycle timers.
 * @brief   mod_desc_t on_ign_on 钩子: 重启周期计时
 */
static void prv_on_ign_on(void)
{
    const u32 now = RTI_GetTick1ms();
    for (u16 i = 0; i < s_tx.tx_count; i++) {
        s_tx.track[i].last_send_tick_ms = now;
    }
}

/**
 * @brief   mod_desc_t tick hook: send cyclic + pending frames.
 * @brief   mod_desc_t tick 钩子: 发送周期帧 + pending 帧
 */
static void prv_tick(void)
{
    if (!s_tx.init_done) return;
    if (!RTI_IsElapsed(RTI_10MS)) return;
    const u32 now = RTI_GetTick1ms();
    for (u16 i = 0; i < s_tx.tx_count; i++) {
        const u16 tmo = s_tx.payloads[i].cycle_ms;
        if (tmo == 0u) {
            /* Event-driven only. */
            if (s_tx.track[i].pending) { prv_send(i); }
            continue;
        }
        if ((now - s_tx.track[i].last_send_tick_ms) >= tmo) {
            prv_send(i);
        }
    }
}

/**
 * @brief   mod_desc_t standby hook: clear pending flags.
 * @brief   mod_desc_t standby 钩子: 清零 pending
 */
static void prv_standby(void)
{
    for (u16 i = 0; i < s_tx.tx_count; i++) {
        s_tx.track[i].pending = 0u;
    }
}

/**
 * @brief   Module descriptor registered in scheduler.c
 * @brief   在 scheduler.c 中注册的模块描述符
 */
const mod_desc_t mod_can_tx = {
    .name      = "can_tx",
    .mcu_init   = prv_mcu_init,
    .wakeup_init = prv_wakeup_init,
    .on_ign_on = prv_on_ign_on,
    .tick      = prv_tick,
    .standby   = prv_standby,
};
/* ---------------------------------------------------------------- *
 *  Public API (preparing payloads and triggering)                  *
 * ---------------------------------------------------------------- */

/**
 * @brief   Fill the payload buffer for a TX message.
 * @brief   填充某条 TX 报文的 payload 缓冲
 *
 * @details `data[0..dlc-1]` is copied into the next free slot for
 *          this message.  The next 10 ms tick sends whatever is in
 *          the buffer at that moment.  If `dlc` exceeds 8 it is
 *          clamped.
 *
 * @param[in]  can_id  IPK message can_id
 * @param[in]  data    Source buffer (at least `dlc` bytes)
 * @param[in]  dlc     Data length (0..8)
 *
 * @return  c02b2_result_t
 * @retval  C02B2_OK         Payload queued
 * @retval  C02B2_ERR_PARAM  can_id not an IPK TX message, or data NULL
 */
c02b2_result_t CanTx_PreparePayload(u32 can_id, const u8 *data, u8 dlc)
{
    if (data == NULL) { return C02B2_ERR_PARAM; }
    if (dlc > 8u) { dlc = 8u; }
    for (u16 slot = 0; slot < s_tx.tx_count; slot++) {
        const u16 msg_idx = s_tx.tx_msg_idx[slot];
        if (can_msg_descs_ipk[msg_idx].can_id != can_id) continue;
        for (u8 i = 0; i < dlc; i++) {
            s_tx.payloads[slot].data[i] = data[i];
        }
        s_tx.payloads[slot].dlc = dlc;
        return C02B2_OK;
    }
    return C02B2_ERR_PARAM;
}

/**
 * @brief   Override the cyclic period for a TX message.
 * @brief   覆盖某条 TX 报文的发送周期
 *
 * @param[in]  can_id     IPK TX message can_id
 * @param[in]  cycle_ms   Period in ms (0 = event-driven only)
 *
 * @return  c02b2_result_t
 * @retval  C02B2_OK        Period set
 * @retval  C02B2_ERR_PARAM can_id not an IPK TX message
 */
c02b2_result_t CanTx_SetCycle(u32 can_id, u16 cycle_ms)
{
    for (u16 slot = 0; slot < s_tx.tx_count; slot++) {
        const u16 msg_idx = s_tx.tx_msg_idx[slot];
        if (can_msg_descs_ipk[msg_idx].can_id != can_id) continue;
        s_tx.payloads[slot].cycle_ms = cycle_ms;
        return C02B2_OK;
    }
    return C02B2_ERR_PARAM;
}

/**
 * @brief   Force immediate send of a single TX frame (event-driven)
 * @brief   强制立即发送一帧（事件驱动）
 *
 * @details Marks the matching entry as pending so that the next
 *          10 ms tick sends it regardless of its cycle. Used for
 *          one-shot responses to diag requests, button events, etc.
 *
 * @param[in]  can_id  11-bit CAN identifier (must match a TX db entry)
 *
 * @return  c02b2_result_t
 * @retval  C02B2_OK         Marked as pending
 * @retval  C02B2_ERR_PARAM  No matching TX entry
 */
c02b2_result_t CanTx_Trigger(u32 can_id)
{
    for (u16 slot = 0; slot < s_tx.tx_count; slot++) {
        const u16 msg_idx = s_tx.tx_msg_idx[slot];
        if (can_msg_descs_ipk[msg_idx].can_id != can_id) {
            continue;
        }
        s_tx.track[slot].pending = 1u;
        return C02B2_OK;
    }
    return C02B2_ERR_PARAM;
}

/* ---------------------------------------------------------------- *
 *  Per-signal payload update                                         *
 * ---------------------------------------------------------------- */

/**
 * @brief   Find a signal descriptor by name within a message's range.
 * @brief   在报文描述符范围内按信号名查找信号描述符
 */
static const can_sig_desc_t *prv_find_sig_in_msg(const can_msg_desc_t *msg, u16 sig_id)
{
    if (msg == NULL) { return NULL; }
    const u16 start = msg->sig_index;
    const u16 end   = (u16)(start + msg->sig_count);
    for (u16 i = start; i < end; i++) {
        const can_sig_desc_t *sig = &can_sig_descs_ipk[i];
        /* Signal descriptor order matches CAN_DB_SIG_* enum order
         * (1-based), so descriptor index (i - 0) maps to (sig_id - 1). */
        if ((u16)(i + 1u) == sig_id) {
            return sig;
        }
    }
    return NULL;
}

/**
 * @brief   Update a single signal inside an existing TX payload.
 * @brief   在已有 TX payload 中更新单个信号
 *
 * @param[in]  can_id  IPK TX message can_id
 * @param[in]  sig_id  CAN_DB_SIG_* signal id belonging to that message
 * @param[in]  value   Physical value (will be quantised per factor/offset)
 *
 * @return  c02b2_result_t
 * @retval  C02B2_OK         Signal packed into the slot's payload
 * @retval  C02B2_ERR_PARAM  can_id not a TX message, sig_id not in it
 */
c02b2_result_t CanTx_EncodeSignal(u32 can_id, u16 sig_id, s32 value)
{
    const u16 slot = prv_find_slot(can_id);
    if (slot == 0xFFFFu) { return C02B2_ERR_PARAM; }
    const u16 msg_idx = s_tx.tx_msg_idx[slot];
    const can_msg_desc_t *msg = &can_msg_descs_ipk[msg_idx];
    const can_sig_desc_t *sig = prv_find_sig_in_msg(msg, sig_id);
    if (sig == NULL) { return C02B2_ERR_PARAM; }
    CanDb_EncodeAndPack(s_tx.payloads[slot].data, sig, value);
    return C02B2_OK;
}

/**
 * @brief   Rebuild a TX payload from every signal of that message,
 *          reading current signal-bus values via Signal_Get().
 * @brief   从 signal bus 重新读所有信号, 全量重建一条 TX 报文的 payload
 *
 * @details First clears the payload to zeros, then for every signal
 *          in the message reads Signal_Get(SIG_CAN_<Name>) and packs
 *          it via CanDb_EncodeAndPack().  Suitable for cyclic frames
 *          whose entire payload is derived from bus state.
 *
 *          Side-effect: the slot's payload is updated synchronously,
 *          so the next 10 ms tick will send whatever was rebuilt
 *          here.
 *
 * @param[in]  can_id  IPK TX message can_id
 *
 * @return  c02b2_result_t
 * @retval  C02B2_OK         Payload fully rebuilt
 * @retval  C02B2_ERR_PARAM  can_id not a TX message
 */
c02b2_result_t CanTx_RebuildFromSignals(u32 can_id)
{
    const u16 slot = prv_find_slot(can_id);
    if (slot == 0xFFFFu) { return C02B2_ERR_PARAM; }
    const u16 msg_idx = s_tx.tx_msg_idx[slot];
    const can_msg_desc_t *msg = &can_msg_descs_ipk[msg_idx];

    /* Zero out payload. */
    for (u8 b = 0; b < 8u; b++) { s_tx.payloads[slot].data[b] = 0u; }

    const u16 start = msg->sig_index;
    const u16 end   = (u16)(start + msg->sig_count);
    for (u16 i = start; i < end; i++) {
        const can_sig_desc_t *sig = &can_sig_descs_ipk[i];
        const u16 db_sig_id       = (u16)(i + 1u);
        const signal_id_t bus_id  = CanDb_DbcSigToBus(db_sig_id);
        const s32 value           = Signal_Get(bus_id);
        CanDb_EncodeAndPack(s_tx.payloads[slot].data, sig, value);
    }
    return C02B2_OK;
}
