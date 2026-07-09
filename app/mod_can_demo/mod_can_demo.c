/**
 * @file    mod_can_demo.c
 * @brief   Demo / bring-up module that exercises the full CAN stack
 * @brief   演练完整 CAN 栈的 demo / 联调模块
 *
 * @details Five things are demonstrated on a 1 s tick:
 *
 *   1. signal-bus read       -> Signal_Get(SIG_CAN_*) + LOG_I
 *   2. timeout-bitmap decode -> walk s_bit_to_can_id[] and the three
 *                              SIG_CAN_RX_TIMEOUT_MAP_LO/HI/HI2 slots
 *   3. raw frame cache       -> CanRx_GetLastRawFrame on a chosen IPK id
 *   4. TX whole payload      -> CanTx_PreparePayload + CanTx_Trigger
 *   5. TX one signal         -> CanTx_EncodeSignal + CanTx_Trigger
 *
 * All demo work targets the IPK test batch (CAN_DB_IPK_*) only.
 */
#include "mod_can_demo.h"

#include "types.h"
#include "rti.h"
#include "signal.h"
#include "can_db.h"            /* can_msg_descs_ipk[] / CanDb_*              */
#include "can_db_ipk_gen.h"    /* CAN_DB_IPK_*_COUNT, signal enum            */
#include "can_rx.h"            /* CanRx_GetLastRawFrame / GetRawFrameCount  */
#include "can_tx.h"            /* CanTx_PreparePayload / EncodeSignal / ... */
#include "drv_api/can/can_if.h"/* can_msg_t                                  */

#define LOG_NAME  "CDEM"
#include "log.h"

/* ---------------------------------------------------------------- *
 *  Demo configuration
 *
 *  Targets are pinned to specific IPK IDs / signal IDs so the log is
 *  deterministic and matches the DBC.  Change here only.
 * ---------------------------------------------------------------- */

/* RX id used by demo #3 (raw frame cache read).  Pick an IPK RX
 * id that the bring-up rig is likely to feed.  0x0286 is
 * BCM_RDoorWindowState (RX, dlc=8) in the IPK DBC.
 */
#define DEMO_RX_ID_RAW           0x0286u

/* TX id used by demo #4 (whole payload).  IPK_STS_Tx = 0x026D (IPK_STS). */
#define DEMO_TX_ID_PAYLOAD       0x026Du

/* TX id used by demo #5 (single signal).  IPK_EngineService = 0x03E9. */
#define DEMO_TX_ID_SIGNAL        0x03E9u

/* Signal id within IPK_EngineService for demo #5.
 * IPK_DayToEngSrv (U16, length 9, factor 1) -> 0..511.
 */
#define DEMO_TX_SIGNAL_ID        CAN_DB_SIG_IPK_DayToEngSrv

/* Demo cadence: print once per second so the UART log stays readable. */
#define DEMO_PERIOD_MS            1000u

/* Private state -------------------------------------------------------- */
static struct {
    bool     inited;
    u32      last_tick_ms;
    u32      sweep_count;
} s_demo;

/* ---------------------------------------------------------------- *
 *  Demo #1: signal-bus read
 * ---------------------------------------------------------------- */

/**
 * @brief   Print one signal line using its db / bus id pair
 * @brief   用 db / bus id 打印一行信号值
 */
static void prv_emit_sig(u16 db_id, signal_id_t bus_id, const char *name)
{
    /* RAW u32 on the bus; consumer uses CanDb_DecodeSignal for physical. */
    const u32 raw = Signal_Get(bus_id);
    (void)db_id;
    LOG_I("  sig %s (bus=%u) raw=0x%08X",
          name, (unsigned)bus_id, (unsigned)raw);
}

static void prv_demo_signals(void)
{
    LOG_I("[1/5] signals (live Signal_Get):");
    /* All demo signals are pulled from IPK RX messages that already
     * exist in the IPK DBC (signal.h -> SIG_CAN_* already populated
     * by app/can/can_rx.c). */
    prv_emit_sig((u16)CAN_DB_SIG_EMS_EngineSpeedRPM,
                 SIG_CAN_EMS_EngineSpeedRPM,   "EMS_EngineSpeedRPM");
    prv_emit_sig((u16)CAN_DB_SIG_ESC_VehicleSpeed,
                 SIG_CAN_ESC_VehicleSpeed,     "ESC_VehicleSpeed");
    prv_emit_sig((u16)CAN_DB_SIG_BMSH_BattSOCDisp,
                 SIG_CAN_BMSH_BattSOCDisp,     "BMSH_BattSOCDisp");
}

/* Forward declarations of file-local helpers (defined later). */
static u32 prv_bit_to_can_id(u32 bit);

/* ---------------------------------------------------------------- *
 *  Demo #2: timeout-bitmap decode
 * ---------------------------------------------------------------- */
static void prv_demo_timeouts(void)
{
    const u32 lo  = Signal_Get(SIG_CAN_RX_TIMEOUT_MAP_LO);
    const u32 hi  = Signal_Get(SIG_CAN_RX_TIMEOUT_MAP_HI);
    const u32 hi2 = Signal_Get(SIG_CAN_RX_TIMEOUT_MAP_HI2);
    /* Count set bits in the three words.  Simple popcount. */
    u32 to_cnt = 0u;
    for (u32 i = 0u; i < 32u; i++) {
        if ((lo  >> i) & 1) { to_cnt++; }
        if ((hi  >> i) & 1) { to_cnt++; }
        if ((hi2 >> i) & 1) { to_cnt++; }
    }
    LOG_I("[2/5] timeouts: LO=0x%08X HI=0x%08X HI2=0x%08X set=%u",
          (unsigned)lo, (unsigned)hi, (unsigned)hi2, (unsigned)to_cnt);

    /* Enumerate the first few set bits back to CAN id (Sentinel
     * mapping; bit-N is stable across DBC reorders). */
    u32 printed = 0u;
    for (u32 bit = 0u; bit < (u32)CAN_BITMAP_MAX && printed < 4u; bit++) {
        const u32 word = (bit < 32u) ? lo : (bit < 64u) ? hi : hi2;
        const u32 shift = (bit < 32u) ? bit : (bit < 64u) ? (bit - 32u) : (bit - 64u);
        if (((word >> shift) & 1) == 0) { continue; }
        /* s_bit_to_can_id is emitted by tools/dbc_parse.py into can_db_ipk_gen.c.
         * Use the runtime helper CanDb_FindIpkById when a name is wanted. */
        const u32 can_id = prv_bit_to_can_id(bit);
        const char *name = (can_id != 0u)
            ? CanDb_FindIpkById(can_id)->name : "unused";
        LOG_I("  bit=%u -> id=0x%X (%s) TIMEOUT",
              (unsigned)bit, (unsigned)can_id, name);
        printed++;
    }
}

/* ---------------------------------------------------------------- *
 *  Demo #3: raw frame cache (last 8-byte payload per IPK id)
 * ---------------------------------------------------------------- */
static void prv_demo_raw_frame(void)
{
    can_msg_t frame;
    const c02b2_result_t r = CanRx_GetLastRawFrame(DEMO_RX_ID_RAW, &frame);
    if (r == C02B2_ERR_NOT_FOUND) {
        LOG_I("[3/5] raw cache: id=0x%X never received yet",
              (unsigned)DEMO_RX_ID_RAW);
        return;
    }
    if (r != C02B2_OK) {
        LOG_W("[3/5] raw cache lookup failed id=0x%X (%d)",
              (unsigned)DEMO_RX_ID_RAW, (int)r);
        return;
    }
    const char *name = CanDb_FindIpkById(frame.id)->name;
    LOG_I("[3/5] raw cache: id=0x%X (%s) dlc=%u bytes = %02X %02X %02X %02X %02X %02X %02X %02X",
          (unsigned)frame.id, name, (unsigned)frame.dlc,
          frame.data[0], frame.data[1], frame.data[2], frame.data[3],
          frame.data[4], frame.data[5], frame.data[6], frame.data[7]);
}

/* ---------------------------------------------------------------- *
 *  Demo #4: TX whole payload (8 raw bytes)
 * ---------------------------------------------------------------- */
static void prv_demo_tx_payload(u32 sweep)
{
    /* A simple rotating byte pattern makes the test easy to spot on
     * the CANalyzer trace. */
    u8 buf[8];
    for (u32 i = 0u; i < 8u; i++) { buf[i] = (u8)((sweep + i) & 0xFFu); }
    const c02b2_result_t r = CanTx_PreparePayload(DEMO_TX_ID_PAYLOAD,
                                                 buf, 8u);
    if (r != C02B2_OK) {
        LOG_W("[4/5] PreparePayload id=0x%X failed (%d)",
              (unsigned)DEMO_TX_ID_PAYLOAD, (int)r);
        return;
    }
    const c02b2_result_t t = CanTx_Trigger(DEMO_TX_ID_PAYLOAD);
    if (t != C02B2_OK) {
        LOG_W("[4/5] Trigger id=0x%X failed (%d)",
              (unsigned)DEMO_TX_ID_PAYLOAD, (int)t);
        return;
    }
    LOG_I("[4/5] tx 0x%X (IPK_STS): 8-byte payload queued and triggered",
          (unsigned)DEMO_TX_ID_PAYLOAD);
}

/* ---------------------------------------------------------------- *
 *  Demo #5: TX single signal (rebuild + trigger)
 * ---------------------------------------------------------------- */
static void prv_demo_tx_signal(u32 sweep)
{
    /* Modulo 512 keeps the value in range for a length=9, factor=1
     * signal so the encode step does not reject out-of-range values. */
    /* The bus carries RAW; demo emits a raw value that
     * IPK_DayToEngSrv (length=9, factor=1, offset=0) accepts.
     * For a DBC `+` (unsigned) signal raw range is 0..511. */
    const u32 v = (sweep * 7u) % 512u;
    const c02b2_result_t e = CanTx_EncodeSignal(DEMO_TX_ID_SIGNAL,
                                                DEMO_TX_SIGNAL_ID, v);
    if (e != C02B2_OK) {
        LOG_W("[5/5] EncodeSignal id=0x%X sig=%u v=%d failed (%d)",
              (unsigned)DEMO_TX_ID_SIGNAL,
              (unsigned)DEMO_TX_SIGNAL_ID, (int)v, (int)e);
        return;
    }
    const c02b2_result_t t = CanTx_Trigger(DEMO_TX_ID_SIGNAL);
    if (t != C02B2_OK) {
        LOG_W("[5/5] Trigger id=0x%X failed (%d)",
              (unsigned)DEMO_TX_ID_SIGNAL, (int)t);
        return;
    }
    LOG_I("[5/5] tx 0x%X (IPK_EngineService): IPK_DayToEngSrv=%d (queued)",
          (unsigned)DEMO_TX_ID_SIGNAL, (int)v);
}

/* ---------------------------------------------------------------- *
 *  Helpers
 * ---------------------------------------------------------------- */

/* Per-bit CAN id lookup table (Sentinel).  Declarared in can_db_ipk_gen.h
 * and defined in can_db_ipk_gen.c.  CAN_BITMAP_MAX is the bitmap width
 * (96 entries, one per SIG_CAN_RX_TIMEOUT_MAP_* bit); sentinel_unused
 * is 0 so an id of 0 means "bit reserved / deleted". */
static u32 prv_bit_to_can_id(u32 bit)
{
    if (bit >= (u32)CAN_BITMAP_MAX) { return 0u; }
    const u32 id = s_bit_to_can_id[bit];
    return (id == 0u) ? 0u : id;
}

/* ---------------------------------------------------------------- *
 *  mod_desc_t hooks
 * ---------------------------------------------------------------- */
static void prv_mcu_init(uint8_t cold_boot)
{
    (void)cold_boot;
    s_demo.inited       = true;
    s_demo.last_tick_ms = 0u;
    s_demo.sweep_count  = 0u;
    LOG_I("init (cold_boot=%u)", (unsigned)cold_boot);
}

static void prv_wakeup_init(void)
{
    LOG_I("wakeup_init");
}

static void prv_on_ign_on(void)
{
    LOG_I("on_ign_on");
}

static void prv_tick(void)
{
    if (!s_demo.inited) { return; }
    const u32 now = RTI_GetTick1ms();
    if ((now - s_demo.last_tick_ms) < DEMO_PERIOD_MS) { return; }
    s_demo.last_tick_ms = now;
    s_demo.sweep_count++;
    const u32 sweep = s_demo.sweep_count;
    LOG_I("=== can_demo sweep #%u (raw cache holds %u) ===",
          (unsigned)sweep, (unsigned)CanRx_GetRawFrameCount());
    prv_demo_signals();
    prv_demo_timeouts();
    prv_demo_raw_frame();
    prv_demo_tx_payload(sweep);
    prv_demo_tx_signal(sweep);
}

static void prv_standby(void)
{
    LOG_I("standby");
}

/* Module descriptor ---------------------------------------------------- */
/**
 * @brief   Module descriptor registered in scheduler.c
 * @brief   在 scheduler.c 中注册的模块描述符
 */
const mod_desc_t mod_can_demo = {
    .name      = "can_demo",
    .mcu_init   = prv_mcu_init,
    .wakeup_init = prv_wakeup_init,
    .on_ign_on = prv_on_ign_on,
    .tick      = prv_tick,
    .standby   = prv_standby,
};
