/**
 * @file    can_rx.c
 * @brief   CAN receive dispatcher + timeout monitor
 * @brief   CAN 接收分发 + 超时监控
 *
 * Drains the FLEXCAN RX ring at 5 ms ticks; for each popped frame
 * looks up the IPK message descriptor in `can_msg_descs_ipk[]` and
 * dispatches it via `CanDb_DispatchByDb`.  Unknown IDs are logged
 * at DEBUG (could be a future frame).
 *
 * A 50 ms timer walks the descriptor table to refresh
 * SIG_CAN_RX_TIMEOUT_MAP.
 */
#include "can_rx.h"
#include "can_if.h"
#include "can_db.h"
#include "rti.h"
#include "signal.h"

#define LOG_NAME  "CRX "
#include "log.h"

#define MAX_RX_TRACKED  64u   /**< bitmap width of SIG_CAN_RX_TIMEOUT_MAP */

typedef struct {
    u32 last_rx_tick_ms;  /**< RTI tick at last successful rx (0 = never) */
} rx_track_t;

static struct {
    bool       init_done;
    rx_track_t track[MAX_RX_TRACKED];
} s_rx;
/* ---------------------------------------------------------------- *
 *  Lookup helpers (DBC-driven)                                      *
 * ---------------------------------------------------------------- */

/**
 * @brief   Find the index of an IPK message in the descriptor table.
 * @brief   在 IPK 报文描述符表中查找索引
 *
 * @details Linear search; the IPK test batch has < 32 entries.
 *          Returns 0xFFFFu when not found.
 *
 * @param[in]  can_id  11-bit standard CAN identifier
 *
 * @return  u16  Index into can_msg_descs_ipk[], or 0xFFFFu
 */
static u16 prv_find_ipk_index(u32 can_id)
{
    for (u16 i = 0; i < (u16)CAN_DB_IPK_MSG_COUNT; i++) {
        if (can_msg_descs_ipk[i].can_id == can_id) {
            return i;
        }
    }
    return 0xFFFFu;
}

/**
 * @brief   Look up the timeout configured for a given message.
 * @brief   查找某条报文配置的超时值
 *
 * @details Reads a per-message timeout from a small static table.
 *          The DBC has no notion of cycle/timeout so we keep a
 *          hand-authored parallel table (see prv_timeout_table[]).
 *
 * @param[in]  idx  Index into can_msg_descs_ipk[]
 *
 * @return  u16  Timeout in milliseconds (0 = do not monitor)
 */
static u16 prv_timeout_for(u16 idx)
{
    if (idx >= (u16)CAN_DB_IPK_MSG_COUNT) {
        return 0u;
    }
    /* Per-message timeout table.  The DBC has no notion of cycle/
     * timeout so this hand-authored parallel table is the source
     * of truth.  Empty by default; populate in a follow-up batch
     * once network analysis is done. */
    static const struct {
        u16  msg_index;
        u16  timeout_ms;
    } prv_timeout_table[1] = { { 0u, 0u } };
    for (u32 i = 0; i < (u32)(sizeof(prv_timeout_table) / sizeof(prv_timeout_table[0])); i++) {
        if (prv_timeout_table[i].msg_index == idx) {
            return prv_timeout_table[i].timeout_ms;
        }
    }
    return 0u;
}

/* ---------------------------------------------------------------- *
 *  Module lifecycle                                                 *
 * ---------------------------------------------------------------- */

/**
 * @brief   mod_desc_t init hook: zero the timeout table.
 * @brief   mod_desc_t init 钩子: 清零超时表
 *
 * @param[in]  cold_boot  1 = cold boot, 0 = warm boot
 */
static void prv_init(u8 cold_boot)
{
    (void)cold_boot;
    for (u32 i = 0; i < MAX_RX_TRACKED; i++) {
        s_rx.track[i].last_rx_tick_ms = 0u;
    }
    s_rx.init_done = true;
    LOG_I("init (cold=%u, registered_rx=%u)", (unsigned)cold_boot, (unsigned)CAN_DB_IPK_RX_COUNT);
}

/**
 * @brief   mod_desc_t on_ign_on hook: clear all timeout flags.
 * @brief   mod_desc_t on_ign_on 钩子: 清零所有超时标志
 */
static void prv_on_ign_on(void)
{
    (void)Signal_Set(SIG_CAN_RX_TIMEOUT_MAP, 0);
}
/**
 * @brief   Drain pending frames from the RX ring and dispatch via
 *          the IPK descriptor table.
 * @brief   排空接收环中的待处理帧, 并通过 IPK 描述符表分发
 *
 * @details For each popped frame:
 *          1. Look up the IPK descriptor by can_id.
 *          2. If found, hand the descriptor + payload to
 *             `CanDb_DispatchByDb` which decodes every signal.
 *          3. Stamp `last_rx_tick_ms` so the timeout monitor sees
 *             the entry as fresh.
 *          Frames whose id is not in the IPK table are logged at
 *          DEBUG (could be a future frame or vendor-specific msg).
 */
static void prv_drain(void)
{
    can_msg_t m;
    u32 drained = 0;
    while (CanIf_PopRx(&m)) {
        drained++;
        const u16 idx = prv_find_ipk_index(m.id);
        if (idx != 0xFFFFu) {
            /* Known frame: hand off to the DBC dispatcher. */
            CanDb_DispatchByDb(&can_msg_descs_ipk[idx], m.data);
            if (idx < MAX_RX_TRACKED) {
                s_rx.track[idx].last_rx_tick_ms = RTI_GetTick1ms();
            }
        } else {
            /* Unknown id -- not necessarily an error. */
            LOG_D("rx id=0x%X (no DBC entry)", (unsigned)m.id);
        }
    }
    if (drained > 0u) {
        LOG_D("drained %u frame(s), pending=%u", (unsigned)drained, (unsigned)CanIf_RxPending());
    }
}

/**
 * @brief   Walk the IPK descriptor table and update
 *          SIG_CAN_RX_TIMEOUT_MAP.
 * @brief   遍历 IPK 描述符表, 更新 SIG_CAN_RX_TIMEOUT_MAP
 *
 * @details For every descriptor with a configured timeout, set
 *          bit `idx` in the bitmap if (now - last_rx_tick_ms[idx])
 *          > timeout_ms.  Bits above MAX_RX_TRACKED are silently
 *          skipped.
 */
static void prv_check_timeouts(void)
{
    const u32 now = RTI_GetTick1ms();
    u32 map = 0u;
    for (u16 i = 0; i < (u16)CAN_DB_IPK_MSG_COUNT; i++) {
        const u16 tmo = prv_timeout_for(i);
        if (tmo == 0u) continue;
        if (i >= MAX_RX_TRACKED) break;
        const u32 last = s_rx.track[i].last_rx_tick_ms;
        if ((now - last) > (u32)tmo) {
            map |= (1u << i);
        }
    }
    (void)Signal_Set(SIG_CAN_RX_TIMEOUT_MAP, (int32_t)map);
}

/**
 * @brief   mod_desc_t tick hook: drain @ 5 ms, timeouts @ 50 ms.
 * @brief   mod_desc_t tick 钩子: 5 ms 排空接收环, 50 ms 检查超时
 */
static void prv_tick(void)
{
    if (!s_rx.init_done) { return; }
    if (RTI_IsElapsed(RTI_5MS))  { prv_drain(); }
    if (RTI_IsElapsed(RTI_50MS)) { prv_check_timeouts(); }
}

/**
 * @brief   mod_desc_t standby hook: clear all timeout flags.
 * @brief   mod_desc_t standby 钩子: 清零所有超时标志
 */
static void prv_standby(void)
{
    (void)Signal_Set(SIG_CAN_RX_TIMEOUT_MAP, 0);
}

/**
 * @brief   Module descriptor registered in scheduler.c
 * @brief   在 scheduler.c 中注册的模块描述符
 */
const mod_desc_t mod_can_rx = {
    .name      = "can_rx",
    .init      = prv_init,
    .on_ign_on = prv_on_ign_on,
    .tick      = prv_tick,
    .standby   = prv_standby,
};