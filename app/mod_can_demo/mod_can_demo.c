/**
 * @file    mod_can_demo.c
 * @brief   mod_can_demo - CAN TX/RX demo (NORMAL mode, external CAN tool for RX)
 *
 * Demonstrates TWO DBC-driven TX paths side by side:
 *
 *   Path A - whole-buffer payload (CanTx_PreparePayload):
 *     Every 1 s, fills an 8-byte payload by hand and pushes it into
 *     the 0x0260 (IPK_SettingRequest) TX slot. The can_tx 10 ms tick
 *     then emits the slot every 200 ms until the next 1 s payload
 *     overwrites it.
 *
 *   Path B - per-signal update (CanTx_EncodeSignal):
 *     Every 1 s, increments a synthetic odometer reading and packs
 *     just that one bit-field into 0x026D (IPK_STS). Other signals
 *     in IPK_STS keep their previous value. This is the call
 *     pattern used by real business modules ("I just computed a new
 *     fuel-level value, please update the IPK_FuelLevelSts field").
 *
 *   RX side - drain the ring buffer every 100 ms:
 *     Logs every frame popped from CanIf_PopRx. With an external
 *     CAN tool sending any DBC-known can_id (e.g. 0x02AF) the
 *     log will show the id, dlc and the first two data bytes.
 *
 * Without an external bus, the driver returns BUSY and the log shows
 * the send path attempted; with an external bus, CANalyzer sees
 * the cyclic payloads and RX log entries for upstream frames.
 */
#include "mod_can_demo.h"

#include "drv_api/can/can_if.h"
#include "can_tx.h"
#include "drv_api/can/can_db_ipk_gen.h"
#include "rti.h"

#define LOG_NAME  "CDEM"
#include "log.h"

/* ---------------------------------------------------------------- *
 *  Demo targets                                                     *
 * ---------------------------------------------------------------- */

/* Path A: whole-buffer payload on IPK_SettingRequest */
#define DEMO_TX_A_CAN_ID    0x0260u   /* IPK_SettingRequest, dlc=8 */
#define DEMO_TX_A_CYCLE_MS  200u

/* Path B: per-signal update on IPK_STS.
 * IPK_STS is index 11 in can_msg_descs_ipk[] (sig_index=102, sig_cnt=19).
 * The first signal is IPK_IPKEngineTotalOdometer at sig_index=102,
 * which gives enum id = sig_index + 1 = 103. */
#define DEMO_TX_B_CAN_ID    0x026Du   /* IPK_STS */
#define DEMO_TX_B_CYCLE_MS  200u
#define DEMO_TX_B_SIG_ID    103u      /* CAN_DB_SIG_IPK_IPKEngineTotalOdometer */
#define DEMO_TX_B_OdometerStepKm  7u  /* increments 7 km per tick (raw value) */

/* RX drain cap: prevent a runaway sender from starving the super-loop. */
#define DEMO_RX_DRAIN_MAX_FRAMES   8u

/* ---------------------------------------------------------------- *
 *  Private state                                                    *
 * ---------------------------------------------------------------- */
static struct {
    uint8_t  init_done;
    uint32_t tx_a_counter;   /* monotonic counter for Path A payload   */
    uint32_t tx_b_odometer;  /* synthetic odometer for Path B signal   */
    uint32_t rx_recv_count;  /* total RX frames drained (any id)       */
} s_ctx;

/* ---------------------------------------------------------------- *
 *  mod_desc_t hooks                                                 *
 * ---------------------------------------------------------------- */

static void prv_init(uint8_t cold_boot)
{
    (void)cold_boot;
    s_ctx.init_done     = 1u;
    s_ctx.tx_a_counter  = 0u;
    s_ctx.tx_b_odometer = 0u;
    s_ctx.rx_recv_count = 0u;
    /* Set cyclic periods so both demo targets emit at 200 ms. */
    (void)CanTx_SetCycle(DEMO_TX_A_CAN_ID, DEMO_TX_A_CYCLE_MS);
    (void)CanTx_SetCycle(DEMO_TX_B_CAN_ID, DEMO_TX_B_CYCLE_MS);
    LOG_I("init (cold=%u, tx_a=0x%X @%ums, tx_b=0x%X @%ums sig=%u)",
          (unsigned)cold_boot,
          (unsigned)DEMO_TX_A_CAN_ID, (unsigned)DEMO_TX_A_CYCLE_MS,
          (unsigned)DEMO_TX_B_CAN_ID, (unsigned)DEMO_TX_B_CYCLE_MS,
          (unsigned)DEMO_TX_B_SIG_ID);
}

static void prv_on_ign_on(void)
{
    LOG_I("on_ign_on");
}

/**
 * @brief Path A: bump counter, push the whole 8-byte payload into the
 *        0x0260 slot via CanTx_PreparePayload.
 *
 * @details Use case: business code has the entire payload already
 *          computed (e.g. by calling CanDb_EncodeAndPack on every
 *          signal) and just wants to hand the bytes over to can_tx.
 */
static void prv_do_1s_prepare_job(void)
{
    s_ctx.tx_a_counter++;
    uint8_t payload[8] = {0};
    payload[0] = (uint8_t)(s_ctx.tx_a_counter & 0xFFu);
    payload[1] = (uint8_t)((s_ctx.tx_a_counter >> 8) & 0xFFu);
    payload[2] = (uint8_t)((s_ctx.tx_a_counter >> 16) & 0xFFu);
    payload[3] = (uint8_t)((s_ctx.tx_a_counter >> 24) & 0xFFu);
    /* Bytes [4..7] stay 0; CANalyzer should show 0x0260 data[0..3]
     * incrementing every 1 s while data[4..7] stay 0. */
    const c02b2_result_t r = CanTx_PreparePayload(DEMO_TX_A_CAN_ID,
                                                  payload, 8u);
    if (r == C02B2_OK) {
        LOG_I("txA prepare ok: 0x%X counter=%u",
              (unsigned)DEMO_TX_A_CAN_ID, (unsigned)s_ctx.tx_a_counter);
    } else {
        LOG_W("txA prepare rejected (%d) for 0x%X",
              (int)r, (unsigned)DEMO_TX_A_CAN_ID);
    }
}

/**
 * @brief Path B: bump synthetic odometer, update ONE bit field in
 *        the 0x026D slot via CanTx_EncodeSignal.
 *
 * @details Use case: a business module just computed a new fuel-level
 *          / odometer / handbrake-state value and only wants to touch
 *          its own field, leaving the other 18 signals of IPK_STS
 *          untouched. Internally this calls CanDb_EncodeAndPack which
 *          reads the current payload, mutates the field, and writes
 *          it back.
 */
static void prv_do_1s_encode_job(void)
{
    s_ctx.tx_b_odometer += DEMO_TX_B_OdometerStepKm;
    const c02b2_result_t r = CanTx_EncodeSignal(DEMO_TX_B_CAN_ID,
                                                DEMO_TX_B_SIG_ID,
                                                (int32_t)s_ctx.tx_b_odometer);
    if (r == C02B2_OK) {
        LOG_I("txB encode ok: 0x%X sig=%u odo=%u km",
              (unsigned)DEMO_TX_B_CAN_ID, (unsigned)DEMO_TX_B_SIG_ID,
              (unsigned)s_ctx.tx_b_odometer);
    } else {
        LOG_W("txB encode rejected (%d) for 0x%X sig=%u",
              (int)r, (unsigned)DEMO_TX_B_CAN_ID,
              (unsigned)DEMO_TX_B_SIG_ID);
    }
}

/**
 * @brief RX drain: pop up to DEMO_RX_DRAIN_MAX_FRAMES frames per call
 *        and log id + first two data bytes.
 *
 * @details This runs every 100 ms independently of the TX jobs so
 *          upstream frames are observed promptly. The cap exists to
 *          bound the time spent in this function under burst load.
 */
static void prv_do_100ms_rx_drain(void)
{
    can_msg_t m;
    uint32_t drained = 0u;
    while (drained < DEMO_RX_DRAIN_MAX_FRAMES && CanIf_PopRx(&m)) {
        s_ctx.rx_recv_count++;
        LOG_I("rx id=0x%X dlc=%u data[0..1]=0x%02X 0x%02X",
              (unsigned)m.id, (unsigned)m.dlc,
              (unsigned)m.data[0], (unsigned)m.data[1]);
        drained++;
    }
    if (drained >= DEMO_RX_DRAIN_MAX_FRAMES) {
        LOG_W("rx drain hit cap (%u) - more frames pending=%u",
              (unsigned)DEMO_RX_DRAIN_MAX_FRAMES,
              (unsigned)CanIf_RxPending());
    }
}

static void prv_tick(void)
{
    if (!s_ctx.init_done) { return; }
    /* Run order rationale:
     *   1. RX drain first - keeps upstream-to-signal latency low and
     *      is bounded by DEMO_RX_DRAIN_MAX_FRAMES so it cannot starve
     *      the rest of the super-loop.
     *   2. TX-prep and TX-encode are both gated on the SAME 1 s
     *      period slot - we must sample it once into a local and
     *      reuse, otherwise the second call returns false because
      *     RTI_IsElapsed() stamps the slot on every true return. */
    if (RTI_IsElapsed(RTI_100MS))  { prv_do_100ms_rx_drain(); }
    if (RTI_IsElapsed(RTI_1000MS)) {
        prv_do_1s_prepare_job();
        prv_do_1s_encode_job();
    }
}

static void prv_standby(void)
{
    LOG_I("standby (rx=%u)", (unsigned)s_ctx.rx_recv_count);
}

/* ---------------------------------------------------------------- *
 *  Module descriptor                                                *
 * ---------------------------------------------------------------- */
const mod_desc_t mod_can_demo = {
    .name      = "can_demo",
    .init      = prv_init,
    .on_ign_on = prv_on_ign_on,
    .tick      = prv_tick,
    .standby   = prv_standby,
};
