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
    u32 last_send_tick_ms;  /* RTI tick at last successful tx        */
    u8  pending;            /* set by Trigger(), cleared after send  */
} tx_track_t;

static struct {
    bool init_done;
    tx_track_t track[MAX_TX_TRACKED];
} s_tx;

/**
 * @brief   Send the i-th entry of g_can_tx_db[] now
 * @brief   立即发送 g_can_tx_db[] 中的第 i 项
 *
 * @details Pack payload, dispatch to CanIf_Send, then stamp
 *          last_send_tick_ms on success. On failure the timestamp
 *          is NOT updated, so the next 10 ms tick will retry.
 *
 * @param[in]  idx  Table index
 */
static void prv_send(u16 idx)
{
    if (idx >= g_can_tx_count) return;
    const can_tx_desc_t *d = &g_can_tx_db[idx];
    /* Skip entries without a packer (shouldn't happen but be defensive). */
    if (d->pack == NULL) return;
    can_msg_t m = {0};
    /* Copy id/ide/dlc and let the packer fill data[]. */
    m.id  = d->can_id;
    m.ide = d->ide;
    m.dlc = d->dlc;
    d->pack(m.data);
    if (CanIf_Send(d->ch, &m) == LBX_OK) {
        /* Stamp the success time; clear any pending flag. */
        s_tx.track[idx].last_send_tick_ms = RTI_GetTick1ms();
        s_tx.track[idx].pending = 0u;
    }
}

/**
 * @brief   mod_desc_t init hook: zero timing and pending state.
 * @brief   mod_desc_t init 钩子：清零计时与 pending 状态
 *
 * @param[in]  cold_boot  1 = cold boot, 0 = warm boot
 */
static void prv_init(u8 cold_boot)
{
    (void)cold_boot;
    /* last_send_tick_ms = 0 forces the first cycle to elapse immediately. */
    for (u32 i = 0; i < MAX_TX_TRACKED; i++) {
        s_tx.track[i].last_send_tick_ms = 0u;
        s_tx.track[i].pending = 0u;
    }
    s_tx.init_done = true;
    LOG_I("init (cold=%u, registered=%u)", (unsigned)cold_boot, (unsigned)g_can_tx_count);
}

/**
 * @brief   mod_desc_t on_ign_on hook: restart cycle timers on IGN edge.
 * @brief   mod_desc_t on_ign_on 钩子：IGN 边沿时重启所有周期计时
 *
 * @details Resetting last_send_tick_ms to "now" delays the next
 *          send to one full period after IGN, so we don't flood
 *          the bus with every frame at the same instant.
 */
static void prv_on_ign_on(void)
{
    /* Reset all periods so frames send soon after IGN. */
    for (u32 i = 0; i < MAX_TX_TRACKED && i < g_can_tx_count; i++) {
        s_tx.track[i].last_send_tick_ms = RTI_GetTick1ms();
    }
}

/**
 * @brief   mod_desc_t tick hook: send cyclic / event-driven frames.
 * @brief   mod_desc_t tick 钩子：发送周期 / 事件驱动帧
 *
 * @details Two modes per entry:
 *          - cycle_ms > 0: send every cycle_ms
 *          - cycle_ms == 0: event-driven; send only when `pending`
 */
static void prv_tick(void)
{
    if (!s_tx.init_done) return;
    /* Throttle to 10 ms to avoid wasting CPU on every super-loop iteration. */
    if (!RTI_IsElapsed(RTI_10MS)) return;
    u32 now = RTI_GetTick1ms();
    for (u32 i = 0; i < g_can_tx_count; i++) {
        if (i >= MAX_TX_TRACKED) break;
        const can_tx_desc_t *d = &g_can_tx_db[i];
        if (d->cycle_ms == 0u) {
            /* Event-driven only: send if a Trigger() has armed it. */
            if (s_tx.track[i].pending) {
                prv_send((u16)i);
            }
            continue;
        }
        /* Cyclic: send when the period has elapsed. */
        if ((now - s_tx.track[i].last_send_tick_ms) >= d->cycle_ms) {
            prv_send((u16)i);
        }
    }
}

/**
 * @brief   mod_desc_t standby hook: no TX while in standby.
 * @brief   mod_desc_t standby 钩子：standby 期间不发送
 */
static void prv_standby(void)
{
    /* No TX while in standby. */
}

/**
 * @brief   Module descriptor registered in scheduler.c
 * @brief   在 scheduler.c 中注册的模块描述符
 */
const mod_desc_t mod_can_tx = {
    .name      = "can_tx",
    .init      = prv_init,
    .on_ign_on = prv_on_ign_on,
    .tick      = prv_tick,
    .standby   = prv_standby,
};

/**
 * @brief   Force immediate send of a single frame (event-driven)
 * @brief   强制立即发送一帧（事件驱动）
 *
 * @details Marks the matching entry in g_can_tx_db[] as pending
 *          so that the next 10 ms tick sends it regardless of
 *          its cycle. Used for one-shot responses to diag
 *          requests, button events, etc.
 *
 * @param[in]  can_id  11/29-bit CAN identifier (must match a db entry)
 *
 * @return  lbx_result_t
 * @retval  LBX_OK         Marked as pending
 * @retval  LBX_ERR_PARAM  No matching entry in g_can_tx_db[]
 */
lbx_result_t CanTx_Trigger(u32 can_id)
{
    for (u32 i = 0; i < g_can_tx_count; i++) {
        if (g_can_tx_db[i].can_id == can_id) {
            /* Set the pending flag; cleared by prv_send() after success. */
            s_tx.track[i].pending = 1u;
            return LBX_OK;
        }
    }
    return LBX_ERR_PARAM;
}
