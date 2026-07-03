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

static const can_rx_cb_t prv_find_cb(u32 can_id, u8 ide)
{
    for (u32 i = 0; i < g_can_rx_count; i++) {
        if (g_can_rx_db[i].can_id == can_id && g_can_rx_db[i].ide == ide) {
            return g_can_rx_db[i].cb;
        }
    }
    return NULL;
}

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
static void prv_init(u8 cold_boot)
{
    (void)cold_boot;
    for (u32 i = 0; i < MAX_RX_TRACKED; i++) {
        s_rx.track[i].last_rx_tick_ms = 0u;
    }
    s_rx.init_done = true;
    LOG_I("init (cold=%u, registered=%u)", (unsigned)cold_boot, (unsigned)g_can_rx_count);
}

static void prv_on_ign_on(void)
{
    /* Clear all timeouts on IGN edge */
    (void)Signal_Set(SIG_CAN_RX_TIMEOUT_MAP, 0);
}

static void prv_drain(void)
{
    can_msg_t m;
    u32 drained = 0;
    while (CanIf_PopRx(&m)) {
        drained++;
        const can_rx_cb_t cb = prv_find_cb(m.id, m.ide);
        if (cb) {
            cb(&m);
            u16 idx = prv_find_index(m.id, m.ide);
            if (idx < MAX_RX_TRACKED) {
                s_rx.track[idx].last_rx_tick_ms = RTI_GetTick1ms();
            }
        } else {
            LOG_D("rx id=0x%X (no cb)", (unsigned)m.id);
        }
    }
    if (drained > 0) {
        LOG_D("drained %u frame(s), pending=%u", (unsigned)drained, (unsigned)CanIf_RxPending());
    }
}

static void prv_check_timeouts(void)
{
    u32 now = RTI_GetTick1ms();
    u32 map = 0;
    for (u32 i = 0; i < g_can_rx_count; i++) {
        u16 tmo = g_can_rx_db[i].timeout_ms;
        if (tmo == 0u) continue;
        if (i >= MAX_RX_TRACKED) break;
        u32 last = s_rx.track[i].last_rx_tick_ms;
        if ((now - last) > tmo) {
            map |= (1u << i);
        }
    }
    (void)Signal_Set(SIG_CAN_RX_TIMEOUT_MAP, (int32_t)map);
}

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

static void prv_standby(void)
{
    (void)Signal_Set(SIG_CAN_RX_TIMEOUT_MAP, 0);
}

const mod_desc_t mod_can_rx = {
    .name      = "can_rx",
    .init      = prv_init,
    .on_ign_on = prv_on_ign_on,
    .tick      = prv_tick,
    .standby   = prv_standby,
};
