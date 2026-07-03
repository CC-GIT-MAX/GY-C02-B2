/**
 * @file    can_tx.c
 * @brief   Cyclic + event-triggered CAN transmit
 *
 * - On 10ms tick, walks g_can_tx_db[] and sends frames whose
 *   cycle_ms period has elapsed.
 * - CanTx_Trigger(id) forces an immediate send of that frame
 *   (use for one-shot responses to diag / button events).
 */
#include "can_tx.h"
#include "can_if.h"
#include "can_db.h"
#include "rti.h"

#define LOG_NAME  "CTX "
#include "log.h"

#define MAX_TX_TRACKED  32u

typedef struct {
    u32 last_send_tick_ms;
    u8  pending;     /* set by Trigger(), cleared after send */
} tx_track_t;

static struct {
    bool init_done;
    tx_track_t track[MAX_TX_TRACKED];
} s_tx;

static void prv_send(u16 idx)
{
    if (idx >= g_can_tx_count) return;
    const can_tx_desc_t *d = &g_can_tx_db[idx];
    if (d->pack == NULL) return;
    can_msg_t m = {0};
    m.id  = d->can_id;
    m.ide = d->ide;
    m.dlc = d->dlc;
    d->pack(m.data);
    if (CanIf_Send(d->ch, &m) == LBX_OK) {
        s_tx.track[idx].last_send_tick_ms = RTI_GetTick1ms();
        s_tx.track[idx].pending = 0u;
    }
}

static void prv_init(u8 cold_boot)
{
    (void)cold_boot;
    for (u32 i = 0; i < MAX_TX_TRACKED; i++) {
        s_tx.track[i].last_send_tick_ms = 0u;
        s_tx.track[i].pending = 0u;
    }
    s_tx.init_done = true;
    LOG_I("init (cold=%u, registered=%u)", (unsigned)cold_boot, (unsigned)g_can_tx_count);
}

static void prv_on_ign_on(void)
{
    /* Reset all periods so frames send soon after IGN */
    for (u32 i = 0; i < MAX_TX_TRACKED && i < g_can_tx_count; i++) {
        s_tx.track[i].last_send_tick_ms = RTI_GetTick1ms();
    }
}

static void prv_tick(void)
{
    if (!s_tx.init_done) return;
    if (!RTI_IsElapsed(RTI_10MS)) return;
    u32 now = RTI_GetTick1ms();
    for (u32 i = 0; i < g_can_tx_count; i++) {
        if (i >= MAX_TX_TRACKED) break;
        const can_tx_desc_t *d = &g_can_tx_db[i];
        if (d->cycle_ms == 0u) {
            /* event-driven only */
            if (s_tx.track[i].pending) {
                prv_send((u16)i);
            }
            continue;
        }
        if ((now - s_tx.track[i].last_send_tick_ms) >= d->cycle_ms) {
            prv_send((u16)i);
        }
    }
}

static void prv_standby(void)
{
    /* No TX while in standby */
}

const mod_desc_t mod_can_tx = {
    .name      = "can_tx",
    .init      = prv_init,
    .on_ign_on = prv_on_ign_on,
    .tick      = prv_tick,
    .standby   = prv_standby,
};

lbx_result_t CanTx_Trigger(u32 can_id)
{
    for (u32 i = 0; i < g_can_tx_count; i++) {
        if (g_can_tx_db[i].can_id == can_id) {
            s_tx.track[i].pending = 1u;
            return LBX_OK;
        }
    }
    return LBX_ERR_PARAM;
}
