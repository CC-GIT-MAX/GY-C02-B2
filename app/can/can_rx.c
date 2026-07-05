/**
 * @file    can_rx.c
 * @brief   CAN receive dispatcher + timeout monitor
 *
 * - Pulls frames from CanIf ring buffer
 * - Looks up matching cb in g_can_rx_db[]
 * - Updates last_rx_tick[ID] and SIG_CAN_RX_TIMEOUT_MAP
 */
#include "can_rx.h"
#include "can_if.h"
#include "can_db.h"
#include "rti.h"
#include "signal.h"

#define LOG_NAME  "CRX "
#include "log.h"

#define MAX_RX_TRACKED  64u   /* bitmap width matches timeout signal */

typedef struct {
    u32 last_rx_tick_ms;  /* RTI tick at last successful rx */
} rx_track_t;

static struct {
    bool init_done;
    rx_track_t track[MAX_RX_TRACKED];
} s_rx;

/**
 * @brief   Look up the RX callback for a (can_id, ide) pair.
 * @brief   根据 (can_id, ide) 在 RX 表中查找回调
 *
 * @details Linear search is fine here: the table has < 32 entries.
 *
 * @param[in]  can_id  CAN identifier (11 or 29 bit)
 * @param[in]  ide     0 = standard 11-bit, 1 = extended 29-bit
 *
 * @return  can_rx_cb_t  Callback or NULL if no match
 */
static const can_rx_cb_t prv_find_cb(u32 can_id, u8 ide)
{
    for (u32 i = 0; i < g_can_rx_count; i++) {
        if (g_can_rx_db[i].can_id == can_id && g_can_rx_db[i].ide == ide) {
            return g_can_rx_db[i].cb;
        }
    }
    return NULL;
}

/**
 * @brief   Look up the table index for a (can_id, ide) pair.
 * @brief   根据 (can_id, ide) 在 RX 表中查找索引
 *
 * @details Returns 0xFFFFu (sentinel) if not found.
 *
 * @param[in]  can_id  CAN identifier
 * @param[in]  ide     0/1 = standard/extended
 *
 * @return  u16  Index into g_can_rx_db[] or 0xFFFFu
 */
static u16 prv_find_index(u32 can_id, u8 ide)
{
    for (u32 i = 0; i < g_can_rx_count; i++) {
        if (g_can_rx_db[i].can_id == can_id && g_can_rx_db[i].ide == ide) {
            return (u16)i;
        }
    }
    return 0xFFFFu;
}

/* mod_desc_t hooks */

/**
 * @brief   mod_desc_t init hook: zero the timeout table.
 * @brief   mod_desc_t init 钩子：清零超时表
 *
 * @param[in]  cold_boot  1 = cold boot, 0 = warm boot
 */
static void prv_init(u8 cold_boot)
{
    (void)cold_boot;
    /* last_rx_tick_ms = 0 means "never received" (timeout fires immediately). */
    for (u32 i = 0; i < MAX_RX_TRACKED; i++) {
        s_rx.track[i].last_rx_tick_ms = 0u;
    }
    s_rx.init_done = true;
    LOG_I("init (cold=%u, registered=%u)", (unsigned)cold_boot, (unsigned)g_can_rx_count);
}

/**
 * @brief   mod_desc_t on_ign_on hook: clear all timeout flags.
 * @brief   mod_desc_t on_ign_on 钩子：清零所有超时标志
 *
 * @details On a new IGN cycle the previous session's timeouts are
 *          not relevant; reset the bitmap so the upper layer
 *          starts from a clean state.
 */
static void prv_on_ign_on(void)
{
    /* Clear all timeouts on IGN edge. */
    (void)Signal_Set(SIG_CAN_RX_TIMEOUT_MAP, 0);
}

/**
 * @brief   Drain pending frames from the RX ring and dispatch callbacks.
 * @brief   排空接收环中的待处理帧并分发回调
 *
 * @details For each popped frame:
 *          1. Look up the cb by (id, ide).
 *          2. Invoke the cb (it publishes to the signal bus).
 *          3. Update last_rx_tick for the entry to mark "fresh".
 *          Frames with no matching cb are logged at DEBUG.
 */
static void prv_drain(void)
{
    can_msg_t m;
    u32 drained = 0;
    while (CanIf_PopRx(&m)) {
        drained++;
        const can_rx_cb_t cb = prv_find_cb(m.id, m.ide);
        if (cb) {
            /* Known frame: invoke user callback to decode payload. */
            cb(&m);
            /* Stamp last-rx time so timeout monitor knows it's fresh. */
            u16 idx = prv_find_index(m.id, m.ide);
            if (idx < MAX_RX_TRACKED) {
                s_rx.track[idx].last_rx_tick_ms = RTI_GetTick1ms();
            }
        } else {
            /* Unknown id: log at DEBUG; not an error (could be a future frame). */
            LOG_D("rx id=0x%X (no cb)", (unsigned)m.id);
        }
    }
    if (drained > 0) {
        LOG_D("drained %u frame(s), pending=%u", (unsigned)drained, (unsigned)CanIf_RxPending());
    }
}

/**
 * @brief   Walk the RX table and update SIG_CAN_RX_TIMEOUT_MAP.
 * @brief   遍历 RX 表并更新 SIG_CAN_RX_TIMEOUT_MAP
 *
 * @details For every entry with a non-zero timeout_ms, set bit `i`
 *          in the bitmap if (now - last_rx_tick_ms[i]) > timeout_ms.
 *          Bits above MAX_RX_TRACKED (64) cannot be represented
 *          and are silently skipped.
 */
static void prv_check_timeouts(void)
{
    u32 now = RTI_GetTick1ms();
    u32 map = 0;
    for (u32 i = 0; i < g_can_rx_count; i++) {
        u16 tmo = g_can_rx_db[i].timeout_ms;
        /* timeout_ms == 0 means "do not monitor" for this id. */
        if (tmo == 0u) continue;
        /* Skip entries beyond our bitmap capacity. */
        if (i >= MAX_RX_TRACKED) break;
        u32 last = s_rx.track[i].last_rx_tick_ms;
        if ((now - last) > tmo) {
            /* Bit `i` set means entry i is currently timing out. */
            map |= (1u << i);
        }
    }
    (void)Signal_Set(SIG_CAN_RX_TIMEOUT_MAP, (int32_t)map);
}

/**
 * @brief   mod_desc_t tick hook: drain @ 5 ms, timeouts @ 50 ms.
 * @brief   mod_desc_t tick 钩子：5ms 排空接收环，50ms 检查超时
 */
static void prv_tick(void)
{
    if (!s_rx.init_done) return;
    if (RTI_IsElapsed(RTI_5MS)) {
        prv_drain();
    }
    if (RTI_IsElapsed(RTI_50MS)) {
        prv_check_timeouts();
    }
}

/**
 * @brief   mod_desc_t standby hook: clear all timeout flags.
 * @brief   mod_desc_t standby 钩子：清零所有超时标志
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
