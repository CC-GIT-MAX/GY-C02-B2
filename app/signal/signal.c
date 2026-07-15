/**
 * @file    signal.c
 * @brief   Signal bus storage and accessors (v0.5 rewrite)
 *
 * Storage: fixed-size u32 array indexed by signal_id_t. Each slot
 * holds the current RAW u32 value. "Validity" (i.e. "is this signal
 * trustworthy right now?") is no longer a per-slot bit; it is derived
 * from the per-MSG timeout bitmap (SIG_CAN_RX_TIMEOUT_MAP_*).
 *
 * New model (v0.5):
 *   - value: per-signal RAW u32 (data only, no validity bookkeeping).
 *   - boot_done: static u8; set by prv_check_timeouts() first tick,
 *     cleared by prv_standby() (KL15 off). Acts as a guard so
 *     Signal_IsValid() returns false until the timeout monitor has run
 *     at least once (the bootstrap window).
 *   - ever_received map (SIG_CAN_RX_EVER_RECEIVED_{LO,HI,HI2}): per-MSG
 *     bit set by prv_drain() each time a frame is received; never reset.
 *     Lets callers tell "currently valid" apart from "ever received".
 *
 * Signal_IsValid(id) = boot_done && (timeout map bit for id is 0).
 * Signal_Set / Signal_Get just read/write value.
 * Signal_Invalidate / Signal_InvalidateAll / Signal_GetStored are
 * removed (no longer needed; the timeout bitmap is the SoT).
 * Signal_Reset(id) is a cold-boot helper that writes
 * can_sig_descs_ipk[id-1].init_value into the slot.
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

void Signal_SetBootDone(void)
{
    s_boot_done = 1u;
}

void Signal_ResetBootDone(void)
{
    s_boot_done = 0u;
}

bool Signal_IsBootDone(void)
{
    return s_boot_done != 0u;
}

/* ---------------------------------------------------------------- *
 *  Set / Get                                                        *
 * ---------------------------------------------------------------- */

c02b2_result_t Signal_Set(signal_id_t id, u32 value)
{
    if (id <= SIG_INVALID || id >= SIG_MAX) {
        return C02B2_ERR_PARAM;
    }
    s_signals[id] = value;
    return C02B2_OK;
}

u32 Signal_Get(signal_id_t id)
{
    if (id <= SIG_INVALID || id >= SIG_MAX) {
        return 0u;
    }
    return s_signals[id];
}

/* Cold-boot helper: writes the DBC-derived init_value into the slot.
 * can_rx.c::prv_mcu_init() calls this for every SIG_CAN_* so that
 * before the first frame arrives the slot already holds the
 * DBC default (e.g. 0xFF for TPMS_FLTyrePr Invalid sentinel). */
void Signal_Reset(signal_id_t id)
{
    if (id <= SIG_INVALID || id >= SIG_MAX) {
        return;
    }
    s_signals[id] = can_sig_descs_ipk[(u16)(id - 1u)].init_value;
}

/* ---------------------------------------------------------------- *
 *  Validity: derived from timeout bitmap                             *
 * ---------------------------------------------------------------- */

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

/* "Has this MSG ever delivered a frame since last standby?"
 * Drives "fallback to last known good value" displays. */
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

/* ---------------------------------------------------------------- *
 *  Name lookup (kept for log readability)                           *
 * ---------------------------------------------------------------- */

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
