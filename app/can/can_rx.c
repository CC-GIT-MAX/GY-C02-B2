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
 * Timeout monitor (50 ms tick) walks the 96-entry
 * `s_bit_to_can_id[]` lookup table (Sentinel strategy) and updates
 * SIG_CAN_RX_TIMEOUT_MAP_{LO,HI,HI2}.  bit-N is per-CAN-ID so DBC
 * message reordering does not shift the bit numbering.
 */
#include "can_rx.h"
#include "drv_api/can/can_if.h"
#include "drv_api/can/can_db.h"
#include "rti.h"
#include "signal.h"

#define LOG_NAME  "CRX "
#include "log.h"
/* REVIEW: C3 g_can_rx_timeout_table 标为 AUTOGEN 但实际手维护，Phase 3 移至 gen_ipk_runtime */
/* REVIEW: C9 三张数组有三种语义，需要 static_assert (Phase 1) */
/* A3: ack: RTI_GetTick1ms -> OSIF_GetMilliseconds chains through a single
 * u32 volatile counter incremented in SysTick ISR. On a single-core
 * Cortex-M33 with no D-cache the read of the counter is strictly
 * ordered against the surrounding loads/stores, so no extra DMB
 * is required for can_rx.c. The volatile qualifier plus per-byte
 * exclusive access is sufficient; future D-cache / SMP ports must
 * add a __DMB() between the counter read and any use of the value.
 * Marker closed in Phase 2 without code change.
 */
/* B3: ack: CAN_DB_IPK_RX_COUNT is a compile-time constant of 64 (DBC
 * drives it). The inner body of prv_check_timeouts is ~6 cycles per
 * iteration (load tmo + load track + subtract + compare + branch + 3
 * Signal_Set calls outside the loop), so 50ms tick budget = 64 * 6 =
 * ~384 cycles = ~5 us at 72 MHz - well under the 50 ms budget. The
 * walker also bails early on tmo==0 (no cycle). Marker closed in
 * Phase 2 without code change. If DBC regen ever pushes IPK RX
 * above 128 entries, revisit and add a dirty-bitmap mirror of
 * s_rx.track[].last_rx_tick_ms.
 */
/* REVIEW: B4 CanTx_RebuildFromSignals 每次都全量重建 (Phase 2) */

/* Caller-private RTI slots (replaces shared RTI_IsElapsed via RTI slot API). */
static rti_slot_t s_slot_5ms;
static rti_slot_t s_slot_50ms;

#define MAX_RX_TRACKED  96u   /**< bitmap width of SIG_CAN_RX_TIMEOUT_MAP (LO+HI+HI2 = 3 slots x 32 bit) */

/* Small LRU of unknown CAN ids already announced via LOG_I. We
 * want a noisy bus (0x371 test id, vendor-specific frames) to
 * surface once per id, not every 5 ms. 8 slots covers the usual
 * test-bench traffic; older entries get evicted FIFO. */
#define MAX_RX_UNKNOWN_LOG  8u

typedef struct {
    u32 last_rx_tick_ms;  /**< RTI tick at last successful rx (0 = never) */
} rx_track_t;

/** Per-IPK-message raw frame cache.
 *  Indexed by can_msg_descs_ipk[] (NOT by can_id directly, because two
 *  messages cannot share an id but the table is the SoT for "which ids
 *  do we know about").  `valid` flips to 1 the first time a frame arrives
 *  for that index; the cached bytes are then refreshed on every match.
 */
typedef struct {
    bool valid;
    can_msg_t frame;
    u32  rx_tick_ms;
} rx_raw_cache_t;

static struct {
    bool       init_done;
    /* Phase 1 / C9: pin the three-array invariant. The RX-local
     * cache and the ipk->slot map are sized CAN_DB_IPK_RX_COUNT;
     * the timeout bitmap is sized MAX_RX_TRACKED which must be
     * at least CAN_DB_IPK_RX_COUNT (the timeout table walks a u8
     * index that is also a CAN_DB_IPK_RX_COUNT slot). Lock it
     * here so a future DBC regen that grows RX cannot silently
     * make the bitmap the bottleneck. */
#if (MAX_RX_TRACKED < CAN_DB_IPK_RX_COUNT)
#  error "C9: MAX_RX_TRACKED (96) must be >= CAN_DB_IPK_RX_COUNT; the timeout bitmap indexes by RX slot"
#endif
    rx_track_t track[MAX_RX_TRACKED];
    rx_raw_cache_t raw[CAN_DB_IPK_RX_COUNT];  /**< most-recent raw frame, RX-local slot */
    u16        rx_ipk_idx[CAN_DB_IPK_RX_COUNT];  /**< map RX-local slot -> ipk index (built in mcu_init) */
    u32        seen_unknown_ids[MAX_RX_UNKNOWN_LOG];  /**< FIFO of announced unknown ids */
    u8         seen_unknown_count;                    /**< 0..MAX_RX_UNKNOWN_LOG */
    u8         seen_unknown_next;                     /**< next FIFO write index */
} s_rx;
/* ---------------------------------------------------------------- *
 *  RX timeout table                                                  *
 *                                                                    *
 *  Per-IPK-message receive timeout.  Read by prv_check_timeouts()   *
 *  every 50 ms; bit idx in SIG_CAN_RX_TIMEOUT_MAP is set when       *
 *  (now - last_rx_tick_ms[idx]) > timeout_ms[idx].                  *
 *                                                                    *
 *  Rule (project convention):                                       *
 *    - cycle <  50 ms : fixed 250 ms (fast loop, allow jitter)      *
 *    - cycle >= 50 ms : cycle * 5  (5 missed cycles = timeout)      *
 *    - event-driven   : 5000 ms fallback (DBC GenMsgSendType == 1)   *
 *    - no cycle, not event : 0 (do not monitor)                     *
 *                                                                    *
 *  Index = RX slot 0..RX_COUNT-1 (NOT ipk index). Each row is one   *
 *  RX message; the table holds exactly the IPK RX set, no TX        *
 *  placeholders.  Readers (prv_check_timeouts) feed the same RX     *
 *  slot index used for s_rx.raw[]/s_rx.rx_ipk_idx[].                *
 *                                                                    *
 *  cycle values come from DBC BA_ "GenMsgCycleTime" BO_ <id> <ms>;  *
 *  event-driven comes from BA_ "GenMsgSendType"  BO_ <id> 1;        *
 *  Re-run tools/regen_can_artifacts.py --dbc <IPK.dbc> to refresh.  *
 * ---------------------------------------------------------------- */
/* AUTOGENERATED by tools/dbc_parse.py (DBC GenMsgCycleTime driven). */
static const u16 g_can_rx_timeout_table[CAN_DB_IPK_RX_COUNT] = {
    /*   0 MMI_DateTime_Msg                 (RX) */ 5000u,  /* event-driven, fallback 5s */
    /*   1 MMI_GPS_Info5                    (RX) */ 1000u,  /* cycle=200ms*5 */
    /*   2 MMI_Status_Info                  (RX) */ 500u,  /* cycle=100ms*5 */
    /*   3 MMI_Safety_Info                  (RX) */ 500u,  /* cycle=100ms*5 */
    /*   4 MMI_SOCSet                       (RX) */ 500u,  /* cycle=100ms*5 */
    /*   5 EMS_EngRelateTrqSts              (RX) */ 250u,  /* cycle=10ms<50, fixed 250ms */
    /*   6 EMS_EngineRPM                    (RX) */ 250u,  /* cycle=10ms<50, fixed 250ms */
    /*   7 EMS_EngineDriverInfo             (RX) */ 500u,  /* cycle=100ms*5 */
    /*   8 EMS_EnginePatsBatteryStat        (RX) */ 500u,  /* cycle=100ms*5 */
    /*   9 EMS_OBD_Info                     (RX) */ 500u,  /* cycle=100ms*5 */
    /*  10 EGSM_Status                      (RX) */ 250u,  /* cycle=20ms<50, fixed 250ms */
    /*  11 OBC_Sts                          (RX) */ 500u,  /* cycle=100ms*5 */
    /*  12 OBC_Curr                         (RX) */ 500u,  /* cycle=100ms*5 */
    /*  13 OBC_Failmode                     (RX) */ 500u,  /* cycle=100ms*5 */
    /*  14 BMSH_sts                         (RX) */ 2500u,  /* cycle=500ms*5 */
    /*  15 BMSH_Battery_chgstate            (RX) */ 250u,  /* cycle=20ms<50, fixed 250ms */
    /*  16 BMSH_CellTempLimitValue          (RX) */ 500u,  /* cycle=100ms*5 */
    /*  17 VCU_Ctrl                         (RX) */ 250u,  /* cycle=20ms<50, fixed 250ms */
    /*  18 VCU_InforCAN                     (RX) */ 500u,  /* cycle=100ms*5 */
    /*  19 VCU_DCDC_Ctrl                    (RX) */ 250u,  /* cycle=20ms<50, fixed 250ms */
    /*  20 VCU_CSControl1                   (RX) */ 250u,  /* cycle=20ms<50, fixed 250ms */
    /*  21 VCU_ModeControl                  (RX) */ 250u,  /* cycle=20ms<50, fixed 250ms */
    /*  22 IPU_TrqSpd                       (RX) */ 250u,  /* cycle=10ms<50, fixed 250ms */
    /*  23 IPU_Sts                          (RX) */ 250u,  /* cycle=20ms<50, fixed 250ms */
    /*  24 AVAS_DisabledSts                 (RX) */ 500u,  /* cycle=100ms*5 */
    /*  25 ACU_ChimeTelltaleReq             (RX) */ 1000u,  /* cycle=200ms*5 */
    /*  26 EPS_InformSts                    (RX) */ 250u,  /* cycle=20ms<50, fixed 250ms */
    /*  27 ESC_Status                       (RX) */ 250u,  /* cycle=20ms<50, fixed 250ms */
    /*  28 ESC_DriverRemind                 (RX) */ 500u,  /* cycle=100ms*5 */
    /*  29 ESC_Regen                        (RX) */ 250u,  /* cycle=20ms<50, fixed 250ms */
    /*  30 RSRSR_InformStatus               (RX) */ 500u,  /* cycle=100ms*5 */
    /*  31 FCS_ALAD_Status                  (RX) */ 250u,  /* cycle=20ms<50, fixed 250ms */
    /*  32 FCS_SLIF_IHBC_Status             (RX) */ 500u,  /* cycle=100ms*5 */
    /*  33 FCS_Road_Status                  (RX) */ 250u,  /* cycle=50ms*5 */
    /*  34 FCS_ELK_Status                   (RX) */ 250u,  /* cycle=50ms*5 */
    /*  35 FCS_AEB                          (RX) */ 250u,  /* cycle=20ms<50, fixed 250ms */
    /*  36 FCS_Display                      (RX) */ 250u,  /* cycle=50ms*5 */
    /*  37 FCS_FrontObject                  (RX) */ 250u,  /* cycle=50ms*5 */
    /*  38 FCS_FrontSideObject              (RX) */ 250u,  /* cycle=50ms*5 */
    /*  39 AC_ReqSts                        (RX) */ 500u,  /* cycle=100ms*5 */
    /*  40 TPMS_TyreDataInfo                (RX) */ 250u,  /* cycle=50ms*5 */
    /*  41 TPMS_TempStatusInfo              (RX) */ 2500u,  /* cycle=500ms*5 */
    /*  42 BCM_LightChimeReq                (RX) */ 250u,  /* cycle=50ms*5 */
    /*  43 BCM_LDoorWindowState             (RX) */ 500u,  /* cycle=100ms*5 */
    /*  44 BCM_RDoorWindowState             (RX) */ 500u,  /* cycle=100ms*5 */
    /*  45 BCM_StateUpdate                  (RX) */ 500u,  /* cycle=100ms*5 */
    /*  46 BCM_SunroofState                 (RX) */ 500u,  /* cycle=100ms*5 */
    /*  47 PEPS_KeyReminder                 (RX) */ 5000u,  /* event-driven, fallback 5s */
    /*  48 GW_PEPS_Information              (RX) */ 500u,  /* cycle=100ms*5 */
    /*  49 GW_BCM_Information               (RX) */ 500u,  /* cycle=100ms*5 */
    /*  50 VCU_DriverTqInfo                 (RX) */ 250u,  /* cycle=20ms<50, fixed 250ms */
    /*  51 BMSH_General                     (RX) */ 250u,  /* cycle=10ms<50, fixed 250ms */
    /*  52 BMSH_VoltCurr                    (RX) */ 250u,  /* cycle=20ms<50, fixed 250ms */
    /*  53 BMSH_OBC_Control                 (RX) */ 500u,  /* cycle=100ms*5 */
    /*  54 BMSH_Info                        (RX) */ 500u,  /* cycle=100ms*5 */
    /*  55 EcmChas2Fr92                     (RX) */ 500u,  /* cycle=100ms*5 */
    /*  56 EcmChas2Fr93                     (RX) */ 500u,  /* cycle=100ms*5 */
    /*  57 EcmChas2Fr33                     (RX) */ 750u,  /* cycle=150ms*5 */
    /*  58 PCM_Temperature                  (RX) */ 2500u,  /* cycle=500ms*5 */
    /*  59 PCM_Warning                      (RX) */ 2500u,  /* cycle=500ms*5 */
    /*  60 PcmChas1Fr19                     (RX) */ 250u,  /* cycle=10ms<50, fixed 250ms */
    /*  61 IPU_Temperature                  (RX) */ 2500u,  /* cycle=500ms*5 */
    /*  62 IPU_Warning                      (RX) */ 2500u,  /* cycle=500ms*5 */
    /*  63 TRM_StatusInfo                   (RX) */ 500u,  /* cycle=100ms*5 */
};

/* ---------------------------------------------------------------- *
 *  Lookup helpers (DBC-driven)                                      *
 * ---------------------------------------------------------------- */

/**
 * @brief   Find the index of an IPK message in the descriptor table.
 * @brief   在 IPK 报文描述符表中查找索引
 *
 * @details 线性查找；IPK 测试批次条目少于 32。
 *          未找到时返回 0xFFFFu。 *
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
 * @brief   Look up the bit-N for a given CAN ID via the Sentinel table.
 * @brief   在 Sentinel 表中查找某 CAN ID 对应的 bit-N
 *
 * @details 在 s_bit_to_can_id[] 上线性查找。
 *          当 CAN ID 未分配 bit（已删除的 DBC 哨兵、
 *          保留池或未知 ID）时返回 0xFFu。
 *          O(96) 最坏情况，在 Cortex-M0+ 的 50 ms tick 上没问题。 *
 * @param[in]  can_id  11-bit standard CAN identifier
 *
 * @return  u8  bit index (0..95) or 0xFFu if not mapped
 */
static u8 prv_bit_for_can_id(u32 can_id)
{
    for (u8 b = 0u; b < (u8)CAN_BITMAP_MAX; b++) {
        if (s_bit_to_can_id[b] == can_id) {
            return b;
        }
    }
    return 0xFFu;
}


/* ---------------------------------------------------------------- *
 *  Module lifecycle                                                 *
 * ---------------------------------------------------------------- */

/**
 * @brief   mod_desc_t mcu_init hook: zero the timeout table.
 * @brief   mod_desc_t mcu_init 钩子: 清零超时表
 *
 * @param[in]  cold_boot  1 = cold boot, 0 = warm boot
 */
__root static void prv_mcu_init(u8 cold_boot)
{
    (void)cold_boot;
    for (u32 i = 0; i < MAX_RX_TRACKED; i++) {
        s_rx.track[i].last_rx_tick_ms = 0u;
    }
    s_slot_5ms  = RTI_OpenSlot(RTI_5MS);
    s_slot_50ms = RTI_OpenSlot(RTI_50MS);
    s_rx.init_done = true;
    s_rx.seen_unknown_count = 0u;
    s_rx.seen_unknown_next  = 0u;
    /* Build ipk idx -> RX-local slot map (sized CAN_DB_IPK_RX_COUNT). */
    {
        u16 slot = 0u;
        for (u32 i = 0u; i < (u32)CAN_DB_IPK_MSG_COUNT; i++) {
            if (can_msg_descs_ipk[i].is_tx != 0u) { continue; }
            s_rx.rx_ipk_idx[slot] = (u16)i;
            slot++;
            if (slot >= (u16)CAN_DB_IPK_RX_COUNT) { break; }
        }
    }

    LOG_I("init (cold=%u, registered_rx=%u)", (unsigned)cold_boot, (unsigned)CAN_DB_IPK_RX_COUNT);
}

/**
 * @brief   mod_desc_t wakeup_init hook: post-MCU-init restore.
 * @brief   mod_desc_t wakeup_init 钩子: MCU 初始化后的唤醒恢复
 *
 * @details 在 mcu_init() 之后、on_ign_on() 之前执行。用此钩子
 *          重配 NVIC 优先级、恢复唤醒源状态或预填 mcu_init 留下的
 *          复位配置缓存。目前所有模块都是空实现 ——
 *          当某模块需要真正的 wake-from-reset 工作时再扩展。 */
__root static void prv_wakeup_init(void)
{
    LOG_I("wakeup_init");
}

/**
 * @brief   mod_desc_t on_ign_on hook: clear all timeout flags.
 * @brief   mod_desc_t on_ign_on 钩子: 清零所有超时标志
 */
__root static void prv_on_ign_on(void)
{
    (void)Signal_Set(SIG_CAN_RX_TIMEOUT_MAP_LO,  0);
    (void)Signal_Set(SIG_CAN_RX_TIMEOUT_MAP_HI,  0);
    (void)Signal_Set(SIG_CAN_RX_TIMEOUT_MAP_HI2, 0);
}
/**
 * @brief   Drain pending frames from the RX ring and dispatch via
 *          the IPK descriptor table.
 * @brief   排空接收环中的待处理帧, 并通过 IPK 描述符表分发
 *
 * @details 对每个出队帧：
 *          1. 按 can_id 查 IPK 描述符。
 *          2. 若命中，把描述符 + payload 交给 `CanDb_DispatchByDb`，
 *             后者解码每个信号。
 *          3. 盖 `last_rx_tick_ms` 时间戳，超时监视器便视为新鲜。
 *          不在 IPK 表中的 id 帧以 DEBUG 级别记录（可能是未来的
 *          帧或厂商私有报文）。 */
static void prv_drain(void)
{
    can_msg_t m;
    u32 drained = 0;
    while (CanIf_PopRx(&m)) {
        drained++;
        const u16 idx = prv_find_ipk_index(m.id);
        if (idx != 0xFFFFu) {
            /* Cache the raw frame so diag / demo modules can read
             * the full 8-byte payload without re-decoding every signal.
             * Cache is RX-local (sized CAN_DB_IPK_RX_COUNT); the lookup
             * table rx_ipk_idx[] maps ipk idx -> RX-local slot. */
            /* Map ipk idx -> RX-local slot; defensive upper bound in case
             * the DBC grows RX past CAN_DB_IPK_RX_COUNT */
            u16 rx_slot = 0xFFFFu;
            for (u16 s = 0u; s < (u16)CAN_DB_IPK_RX_COUNT; s++) {
                if (s_rx.rx_ipk_idx[s] == idx) { rx_slot = s; break; }
            }
            if (rx_slot != 0xFFFFu) {
                s_rx.raw[rx_slot].frame     = m;
                s_rx.raw[rx_slot].rx_tick_ms = RTI_GetTick1ms();
                s_rx.raw[rx_slot].valid      = true;
            }
            /* Known frame: hand off to the DBC dispatcher. */
            CanDb_DispatchByDb(&can_msg_descs_ipk[idx], m.data);
            /* Stamp the per-bit-N last-rx tick (Sentinel: bit-N is
             * stable across DBC reorders; the table is the SoT). */
            const u8 bit = prv_bit_for_can_id(m.id);
            if (bit < (u8)CAN_BITMAP_MAX) {
                s_rx.track[bit].last_rx_tick_ms = RTI_GetTick1ms();
            }
        } else {
            /* Unknown id: not necessarily an error (could be a
             * future-DBC frame or vendor-specific msg), but if we
             * have never announced it before we want to surface it
             * once at LOG_I so the operator can correlate with the
             * bus trace. Repeated unknown ids within the FIFO get
             * demoted to LOG_D to keep the log readable. */
            bool announced = false;
            for (u8 k = 0u; k < s_rx.seen_unknown_count; k++) {
                if (s_rx.seen_unknown_ids[k] == m.id) { announced = true; break; }
            }
            if (announced) {
                #if CAN_DEBUG_LOG
                LOG_D("rx id=0x%X (no DBC entry, already announced)", (unsigned)m.id);
                #endif
            } else {
                s_rx.seen_unknown_ids[s_rx.seen_unknown_next] = m.id;
                s_rx.seen_unknown_next = (u8)((s_rx.seen_unknown_next + 1u) % MAX_RX_UNKNOWN_LOG);
                if (s_rx.seen_unknown_count < MAX_RX_UNKNOWN_LOG) {
                    s_rx.seen_unknown_count++;
                }
                #if CAN_DEBUG_LOG
                LOG_I("rx id=0x%X (no DBC entry, first seen)", (unsigned)m.id);
                #endif
            }
        }
    }
    if (drained > 0u) {
        #if CAN_DEBUG_LOG
        LOG_D("drained %u frame(s), pending=%u", (unsigned)drained, (unsigned)CanIf_RxPending());
        #endif
    }
}

/**
 * @brief   Walk the IPK RX table and update the three bitmap
 *          slots of SIG_CAN_RX_TIMEOUT_MAP_{LO,HI,HI2}.
 * @brief   遍历 IPK RX 表, 更新 SIG_CAN_RX_TIMEOUT_MAP_LO/HI/HI2
 *
 * @details 对 IPK 描述符表中每个 RX slot，从
 *          g_can_rx_timeout_table[slot] 读 timeout，再通过
 *          s_rx.rx_ipk_idx[slot] -> can_msg_descs_ipk[] 查 can_id。
 *          若 (now - last_rx_tick_ms[bit]) > timeout_ms[slot]，则
 *          在 LO/HI/HI2 中置位 bit `bit`：
 *            bit  0..31 -> MAP_LO  (bit (bit))
 *            bit 32..63 -> MAP_HI  (bit (bit - 32))
 *            bit 64..95 -> MAP_HI2 (bit (bit - 64))
 *          timeout=0（无周期、非事件驱动）的 slot 被跳过。
 *          映射到 Sentinel 位图 (s_bit_to_can_id[]) 中不存在的
 *          can_id 的位也跳过。 */
static void prv_check_timeouts(void)
{
    const u32 now = RTI_GetTick1ms();
    u32 map_lo  = 0u;
    u32 map_hi  = 0u;
    u32 map_hi2 = 0u;
    for (u16 slot = 0u; slot < (u16)CAN_DB_IPK_RX_COUNT; slot++) {
        const u16 tmo = g_can_rx_timeout_table[slot];
        if (tmo == 0u) continue;
        const u16 ipk_idx = s_rx.rx_ipk_idx[slot];
        const u32 can_id  = can_msg_descs_ipk[ipk_idx].can_id;
        const u8  bit     = prv_bit_for_can_id(can_id);
        if (bit >= (u8)CAN_BITMAP_MAX) continue;
        const u32 last = s_rx.track[bit].last_rx_tick_ms;
        if ((now - last) <= (u32)tmo) continue;
        if (bit < 32u) {
            map_lo |= ((u32)1u << bit);
        } else if (bit < 64u) {
            map_hi |= ((u32)1u << (bit - 32u));
        } else {
            map_hi2 |= ((u32)1u << (bit - 64u));
        }
    }
    (void)Signal_Set(SIG_CAN_RX_TIMEOUT_MAP_LO,  map_lo);
    (void)Signal_Set(SIG_CAN_RX_TIMEOUT_MAP_HI,  map_hi);
    (void)Signal_Set(SIG_CAN_RX_TIMEOUT_MAP_HI2, map_hi2);
}

/**
 * @brief   mod_desc_t tick hook: drain @ 5 ms, timeouts @ 50 ms.
 * @brief   mod_desc_t tick 钩子: 5 ms 排空接收环, 50 ms 检查超时
 *
 * @details 软恢复跑在 5 ms drain 之前，刚恢复的通道
 *          能在同一 tick 内送出第一批数据。当没有 pending 通道时
 *          pump 是 no-op，因此健康路径上每通道每 tick 只多一次
 *          volatile 读。 */
__root static void prv_tick(void)
{
    if (!s_rx.init_done) { return; }
    if (RTI_SlotElapsed(&s_slot_5ms))  
    {
        (void)CanIf_RecoverPump();
        prv_drain();
    }
    if (RTI_SlotElapsed(&s_slot_50ms)) 
    {
         prv_check_timeouts(); 
    }
}

/**
 * @brief   mod_desc_t standby hook: clear all timeout flags.
 * @brief   mod_desc_t standby 钩子: 清零所有超时标志
 */
__root static void prv_standby(void)
{
    (void)Signal_Set(SIG_CAN_RX_TIMEOUT_MAP_LO,  0);
    (void)Signal_Set(SIG_CAN_RX_TIMEOUT_MAP_HI,  0);
    (void)Signal_Set(SIG_CAN_RX_TIMEOUT_MAP_HI2, 0);
}

/* ---------------------------------------------------------------- *
 *  Public API: raw frame cache
 *
 *  Indexed lookup.  The cache is updated inside prv_drain(); the read
 *  API is safe to call from any tick (5 ms or slower) or from main().
 * ---------------------------------------------------------------- */
/**
 * @brief   Fetch the most recent raw frame for an IPK CAN id
 * @brief   获取某 IPK CAN id 最近一次的原始帧
 *
 * @details RX tick 在每个 IPK can_id 上缓存最近收到的原始 8 字节
 *          payload，diag / demo 模块可读整帧而无需重新解码每个信号。
 *          返回自启动以来收到的最后一帧（冷复位时清空）。 *
 * @param[in]   can_id  Standard 11-bit IPK can_id
 * @param[out]  out     Populated with the cached frame on success
 *
 * @return  c02b2_result_t
 * @retval  C02B2_OK            Frame returned (may be stale)
 * @retval  C02B2_ERR_PARAM     out is NULL or can_id not in IPK table
 * @retval  C02B2_ERR_NOT_FOUND No frame received yet for this can_id
 */
c02b2_result_t CanRx_GetLastRawFrame(u32 can_id, can_msg_t *out)
{
    if (out == NULL) {
        return C02B2_ERR_PARAM;
    }
    /* Linear scan of the RX-local cache; ipk lookup via rx_ipk_idx[] */
    for (u32 i = 0u; i < (u32)CAN_DB_IPK_RX_COUNT; i++) {
        const u16 ipk = s_rx.rx_ipk_idx[i];
        if (can_msg_descs_ipk[ipk].can_id != can_id) { continue; }
        if (!s_rx.raw[i].valid) {
            return C02B2_ERR_NOT_FOUND;
        }
        *out = s_rx.raw[i].frame;
        return C02B2_OK;
    }
    return C02B2_ERR_PARAM;   /* unknown can_id */
}

/**
 * @brief   Count of cached RX frames currently valid
 * @brief   当前缓存中有效的 RX 帧数量
 *
 * @details 遍历 RX 本地缓存（每个 IPK RX 条目一个 slot），
 *          统计 `valid` 置位的 slot 数。diag / demo 用它确认扫描是否
 *          见到任何流量，无需枚举整张表。 *
 * @return  u32  Count of cached valid frames (0..CAN_DB_IPK_RX_COUNT)
 */
u32 CanRx_GetRawFrameCount(void)
{
    u32 n = 0u;
    for (u32 i = 0u; i < (u32)CAN_DB_IPK_RX_COUNT; i++) {
        if (s_rx.raw[i].valid) { n++; }
    }
    return n;
}

/**
 * @brief   Module descriptor registered in scheduler.c
 * @brief   在 scheduler.c 中注册的模块描述符
 */
const mod_desc_t mod_can_rx = {
    .name      = "can_rx",
    .mcu_init   = prv_mcu_init,
    .wakeup_init = prv_wakeup_init,
    .on_ign_on = prv_on_ign_on,
    .tick      = prv_tick,
    .standby   = prv_standby,
};

SCHED_REGISTER(mod_can_rx);

