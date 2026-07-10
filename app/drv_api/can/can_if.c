/**
 * @file    can_if.c
 * @brief   CAN interface implementation
 *
 * Bridges the app/can[/] layer to the vendor flexcan_driver.
 * Only this file is allowed to include "flexcan_driver.h".
 *
 * Data flow (RX):
 *   flexcan ISR  -> enqueue to ring buffer (lock-free single producer)
 *   can_rx tick  -> dequeue, dispatch via can_msg_t::cb
 *
 * Data flow (TX):
 *   can_tx tick  -> fill payload via pack() -> CanIf_Send()
 */
#include "can_if.h"

#include "sdk_project_config.h"
#include "flexcan_driver.h"
#include "interrupt_manager.h"

#define LOG_NAME  "CIF "
#include "log.h"
#include "signal.h"
#include "can_db.h"           /* extern can_msg_descs_ipk[]                   */
#include "can_db_ipk_gen.h"   /* CAN_DB_IPK_MSG_COUNT / _RX_COUNT etc.       */

#include "osif.h"           /* OSIF_GetMilliseconds for busy-warn dedup */

/* -------------------------------------------------------------------- *
 *  Ring buffer of received frames
 * -------------------------------------------------------------------- */
#define CAN_RX_RING_SIZE  16u

typedef struct {
    can_msg_t slot[CAN_RX_RING_SIZE];
    volatile u8 head;   /* producer (ISR) writes here                */
    volatile u8 tail;   /* consumer (tick) reads here                */
} can_rx_ring_t;

static can_rx_ring_t s_rx_ring;

/** Reusable scratch buffer used when arming single-MB RX. */
static flexcan_msgbuff_t s_rx_arm_buf;

/** Reusable scratch buffer used to kick off RX-FIFO reception.
 *  The buffer is handed to FLEXCAN_DRV_RxFifo() once at init;
 *  the driver uses it as the destination for the first frame,
 *  then replaces it on every FLEXCAN_EVENT_RXFIFO_COMPLETE. */
static flexcan_msgbuff_t s_rx_fifo_start_buf;

/** Tracks whether CanIf_Init has already been processed. The
 *  install-callback work must run exactly once per boot. */
static bool s_if_inited = false;

/**
 * @brief   Lock-free single-producer push; drops frame on overflow.
 * @brief   无锁单生产者入队；环满时丢弃最新帧
 *
 * @details Producer (ISR) increments `head` after writing.
 *          When `head + 1 == tail` the ring is full (we keep one
 *          slot empty to distinguish full vs empty).
 *
 * @param[in]  m  Frame to push
 *
 * @return  bool
 * @retval  true   Frame accepted
 * @retval  false  Ring full (frame dropped)
 */
static bool prv_ring_push(const can_msg_t *m)
{
    /* Calculate next write position, wrap around. */
    u8 next = (u8)((s_rx_ring.head + 1u) % CAN_RX_RING_SIZE);
    /* Full check: leaving one slot empty to distinguish from empty. */
    if (next == s_rx_ring.tail) {
        return false;  /* full - silently drop, ISR cannot block */
    }
    /* Write the frame BEFORE publishing head (release semantics). */
    s_rx_ring.slot[s_rx_ring.head] = *m;
    /* Publish to consumer: head now points to the next free slot. */
    s_rx_ring.head = next;
    return true;
}

/**
 * @brief   Lock-free single-consumer pop.
 * @brief   无锁单消费者出队
 *
 * @details Consumer (tick) increments `tail` after reading.
 *          `head == tail` means empty.
 *
 * @param[out]  m  Populated on success
 *
 * @return  bool
 * @retval  true   Frame popped into `m`
 * @retval  false  Ring empty
 */
static bool prv_ring_pop(can_msg_t *m)
{
    /* Snapshot both volatile indices into locals to avoid Pa082
     * ("order of volatile accesses undefined") when comparing them
     * in the same expression. Each field is touched exactly once. */
    u8 head = s_rx_ring.head;
    u8 tail = s_rx_ring.tail;
    /* Empty check: head equals tail when all slots consumed. */
    if (head == tail) {
        return false;  /* empty */
    }
    /* Read frame, then advance tail (acquire on next iteration). */
    *m = s_rx_ring.slot[tail];
    s_rx_ring.tail = (u8)((tail + 1u) % CAN_RX_RING_SIZE);
    return true;
}

/* -------------------------------------------------------------------- *
 *  flexcan glue
 * -------------------------------------------------------------------- */

/**
 * @brief   Convert a vendor flexcan_msgbuff_t into a portable can_msg_t.
 * @brief   将 vendor 的 flexcan_msgbuff_t 转换为平台无关的 can_msg_t
 *
 * @details The YTM32B1M flexcan driver populates mb->msgId, mb->dataLen
 *          and mb->data[] directly when FLEXCAN_DRV_Receive returns. The
 *          hardware CS word is driver-internal and not meant to be parsed
 *          by application code. We therefore ignore mb->cs and trust the
 *          SDK-filled fields. ide/rtr are reported as 0 (STD + data) which
 *          is the only frame type the IPK DBC uses today.
 *
 * @param[in]  mb   Vendor mailbox descriptor (filled by FLEXCAN_DRV_Receive)
 * @param[out] out  Portable frame descriptor to fill
 */
static void prv_extract_msg(const flexcan_msgbuff_t *mb, can_msg_t *out)
{
    out->id  = mb->msgId;
    out->ide = 0u;            /* IPK DBC uses 11-bit STD only */
    out->rtr = 0u;            /* IPK DBC uses data frames only */
    /* Clamp DLC to the portable frame size (8 bytes classic CAN). */
    u32 dlc = (mb->dataLen <= 8u) ? mb->dataLen : 8u;
    out->dlc = (u8)dlc;
    /* Copy payload bytes [0..dlc), zero-pad the rest. */
    for (u32 i = 0; i < dlc; i++) {
        out->data[i] = mb->data[i];
    }
    for (u32 i = dlc; i < 8u; i++) {
        out->data[i] = 0u;
    }
}

/**
 * @brief   Map a logical channel to its flexcan instance index.
 * @brief   逻辑通道到 flexcan 实例编号的映射
 *
 * @details The board/can_config.h defines instance numbers via
 *          private_can_INST / public_can_INST macros.
 */
static u8 prv_logical_to_inst(can_channel_t ch)
{
    /* Match board/can_config.h naming. */
    return (ch == CAN_CH_PUBLIC) ? public_can_INST : private_can_INST;
}

/**
 * @brief   flexcan driver callback (runs in ISR context)
 * @brief   flexcan 驱动回调（运行于 ISR 上下文）
 *
 * @details On RX-FIFO complete, the frame is copied out and
 *          pushed to the lock-free ring. Overflows are silently
 *          dropped because the ISR cannot block.
 *
 * @param[in]  instance  flexcan instance index
 * @param[in]  ev        Event type (RX/TX/error)
 * @param[in]  buffIdx   Mailbox index
 * @param[in]  state     Driver state (unused)
 */
static void prv_flexcan_cb(u8 instance,
                           flexcan_event_type_t ev,
                           u32 buffIdx,
                           flexcan_state_t *state)
{
    (void)state;
    switch (ev) {
        case FLEXCAN_EVENT_RXFIFO_COMPLETE: {
            /* RX-FIFO completion.
             *
             * By the time we get here:
             *   1. SDK ISR has called FLEXCAN_ReadRxFifo() into
             *      state->mbs[RXFIFO].mb_message, which is still
             *      &s_rx_fifo_start_buf (we re-arm with the SAME pointer).
             *      So the new frame is sitting in s_rx_fifo_start_buf.
             *   2. SDK has set state->mbs[RXFIFO].state = IDLE.
             *   3. SDK will, on return, check the state again: if it is
             *      still IDLE it calls FLEXCAN_CompleteRxMessageFifoData()
             *      which DISABLES the FRAME_AVAILABLE interrupt -- and then
             *      we never receive another frame.
             *
             * So we MUST call FLEXCAN_DRV_RxFifo() before returning to flip
             * the state back to RX_BUSY.  Crucially, we pass the SAME buffer
             * (&s_rx_fifo_start_buf), not a stack-local: the SDK would
             * otherwise rewrite mb_message to the stack address and the next
             * ISR would write the next frame there.
             *
             * Reading s_rx_fifo_start_buf AFTER FLEXCAN_DRV_RxFifo() is OK
             * because the SDK only writes the buffer inside ISR (FLEXCAN_
             * ReadRxFifo); the arm call just flips state and re-enables
             * IRQs without touching the buffer contents.
             */
            {
                can_msg_t m;
                prv_extract_msg(&s_rx_fifo_start_buf, &m);
                /* Re-arm with the SAME buffer so the SDK keeps writing
                 * new frames here on subsequent IRQs.  This also flips
                 * state back to RX_BUSY so the SDK does NOT call
                 * FLEXCAN_CompleteRxMessageFifoData() (which would mask
                 * the FRAME_AVAILABLE interrupt and stop further RX). */
                (void)FLEXCAN_DRV_RxFifo(instance, &s_rx_fifo_start_buf);
                if (!prv_ring_push(&m)) {
                    /* Ring full - drop and let the drain count surface it. */
                    LOG_W("rx ring full (inst=%u id=0x%X dropped)",
                          (unsigned)instance, (unsigned)m.id);
                }
            }
            break;
        }
        case FLEXCAN_EVENT_RXFIFO_WARNING: {
            /* 5+ frames waiting - either bus is busy or consumer (tick)
             * is starved.  Log once per warning event so the operator can
             * correlate. */
            LOG_W("rx fifo warning (inst=%u) - consumer slow?", (unsigned)instance);
            break;
        }
        case FLEXCAN_EVENT_RXFIFO_OVERFLOW: {
            /* At least one frame was overwritten by the hardware before
             * we drained it.  This is a hard error: a real-world frame
             * is lost.  Log + bump an internal counter (future patch:
             * publish to signal bus). */
            LOG_E("rx fifo OVERFLOW (inst=%u) - frame(s) lost!", (unsigned)instance);
            break;
        }
        case FLEXCAN_EVENT_RX_COMPLETE: {
            /* Single-MB RX (a Mailbox configured via CanIf_ConfigRxMb).
             * buffIdx is the MB index; pull the frame from it. */
            flexcan_msgbuff_t mb;
            if (FLEXCAN_DRV_Receive(instance, (u8)buffIdx, &mb) == STATUS_SUCCESS) {
                can_msg_t m;
                prv_extract_msg(&mb, &m);
                if (!prv_ring_push(&m)) {
                    LOG_W("rx ring full (inst=%u mb=%u id=0x%X dropped)",
                          (unsigned)instance, (unsigned)buffIdx,
                          (unsigned)m.id);
                }
                /* Re-arm the MB so the next match writes into it again. */
                (void)FLEXCAN_DRV_Receive(instance, (u8)buffIdx, &mb);
            }
            break;
        }
        case FLEXCAN_EVENT_TX_COMPLETE: {
            /* Round-robin TX MB finished - no action needed, the next
             * CanIf_Send call will pick an idle MB via prv_pick_tx_mb(). */
            break;
        }
        default:
            /* Errors / wakeup handled by separate error callback. */
            break;
    }
}

/* -------------------------------------------------------------------- *
 *  Error callback (runs in ISR context)                                *
 *                                                                    *
 *  Subscribes to FLEXCAN_DRV_InstallErrorCallback.  The SDK calls    *
 *  us whenever ESR1 fires one of the interrupt bits (BUS_OFF_ENTER,  *
 *  BUS_OFF_DONE, TX_WARNING, RX_WARNING, BIT_ERROR, ERROR_OVERRUN,   *
 *  RAM_ECC).  We log the event and publish bus-health signals so     *
 *  consumers (diag, NM, meter telltales) can react.                  *
 * -------------------------------------------------------------------- */

/** Per-channel cumulative bus-off enter counter (incremented on each
 *  BUS_OFF_ENTER transition; not cleared on recovery so the total is
 *  preserved across recoveries). */
static u32 s_bus_off_count[CAN_CH_MAX] = { 0u, 0u };

/**
 * @brief   Map a flexcan instance index to our logical channel
 * @brief   按 flexcan 实例号映射到逻辑通道
 */
static can_channel_t prv_inst_to_logical(u8 instance)
{
    /* public_can_INST=2, private_can_INST=1; everything else maps to PUBLIC
     * (defensive default for future instances). */
    if (instance == private_can_INST) return CAN_CH_PRIVATE;
    return CAN_CH_PUBLIC;
}

/**
 * @brief   flexcan error callback (runs in ISR context)
 * @brief   flexcan 错误回调（运行于 ISR 上下文）
 *
 * @details Handles the four events the cluster actually cares about:
 *   - BUS_OFF_ENTER  -> log error, set SIG_CAN_BUS_OFF=1, bump counter
 *   - BUS_OFF_DONE   -> log info, clear SIG_CAN_BUS_OFF=0
 *   - TX_WARNING     -> log warning with current TX error counter
 *   - RX_WARNING     -> log warning with current RX error counter
 *   - others         -> log debug
 *
 * @param[in]  instance   flexcan instance index
 * @param[in]  eventType  which ESR1 bit fired
 * @param[in]  state      Driver state (unused)
 */
static void prv_flexcan_err_cb(u8 instance,
                               flexcan_error_event_type_t eventType,
                               flexcan_state_t *state)
{
    (void)state;
    const can_channel_t ch = prv_inst_to_logical(instance);

    /* Read ESR1 once: it carries TX/RX error counters and the live
     * status bits.  Per YTM32B1M ref manual, [TXERRCNT:24-31] and
     * [RXERRCNT:16-23]. */
    const u32 esr1 = FLEXCAN_DRV_GetErrorStatus(instance);
    const u32 tx_err = (esr1 >> 24) & 0xFFu;
    const u32 rx_err = (esr1 >> 16) & 0xFFu;
    (void)Signal_Set(SIG_CAN_TX_ERR_CNT, tx_err);
    (void)Signal_Set(SIG_CAN_RX_ERR_CNT, rx_err);

    switch (eventType) {
        case FLEXCAN_BUS_OFF_ENTER_EVENT: {
            /* The controller has gone bus-off: it has stopped
             * participating in the bus.  Per CAN ISO 11898 the
             * controller stays in this state for at least 128
             * occurrences of 11 consecutive recessive bits before
             * BUS_OFF_DONE fires.  We cannot recover from here in
             * software - just flag it. */
            s_bus_off_count[ch]++;
            (void)Signal_Set(SIG_CAN_BUS_OFF, 1);
            (void)Signal_Set(SIG_CAN_BUS_OFF_COUNT,
                             s_bus_off_count[ch]);
            LOG_E("CAN%u BUS_OFF enter (tx_err=%u rx_err=%u, total=%u)",
                  (unsigned)instance,
                  (unsigned)tx_err, (unsigned)rx_err,
                  (unsigned)s_bus_off_count[ch]);
            break;
        }
        case FLEXCAN_BUS_OFF_DONE_EVENT: {
            /* The controller has recovered from bus-off and is back
             * on the bus.  Clear the flag - normal TX can resume. */
            (void)Signal_Set(SIG_CAN_BUS_OFF, 0);
            LOG_W("CAN%u BUS_OFF done -> back on bus", (unsigned)instance);
            break;
        }
        case FLEXCAN_TX_WARNING_EVENT: {
            /* TX error counter crossed the warning threshold (96).
             * Bus is degraded but not yet bus-off. */
            LOG_W("CAN%u TX warning (tx_err=%u)", (unsigned)instance,
                  (unsigned)tx_err);
            break;
        }
        case FLEXCAN_RX_WARNING_EVENT: {
            /* RX error counter crossed the warning threshold (96). */
            LOG_W("CAN%u RX warning (rx_err=%u)", (unsigned)instance,
                  (unsigned)rx_err);
            break;
        }
        case FLEXCAN_BIT_ERROR_EVENT: {
            LOG_W("CAN%u bit error (tx_err=%u rx_err=%u)",
                  (unsigned)instance, (unsigned)tx_err, (unsigned)rx_err);
            break;
        }
        case FLEXCAN_ERROR_OVERRUN_EVENT: {
            /* RX overrun on a single MB - the MB was overwritten
             * before software read it.  Should not happen with FIFO
             * mode + 16-frame ring, but log if it does. */
            LOG_E("CAN%u overrun - frame(s) lost on MB",
                  (unsigned)instance);
            break;
        }
#if FEATURE_CAN_HAS_RAM_ECC
        case FLEXCAN_RAM_ECC_ERROR_EVENT: {
            /* CAN RAM ECC error - hardware fault, log at error level. */
            LOG_E("CAN%u RAM ECC error - controller in fault",
                  (unsigned)instance);
            break;
        }
#endif
        case FLEXCAN_WAKEUP_EVENT: {
            /* Wake-up from low-power - log at info. */
            LOG_I("CAN%u wakeup event", (unsigned)instance);
            break;
        }
        default:
            LOG_D("CAN%u error event=0x%X (tx_err=%u rx_err=%u)",
                  (unsigned)instance, (unsigned)eventType,
                  (unsigned)tx_err, (unsigned)rx_err);
            break;
    }
}

/* -------------------------------------------------------------------- *
 *  TX mailbox round-robin                                                *
 *                                                                     *
 *  Per YTM32B1M Table 18.20:                                          *
 *    public_can  RFFN=8 (72 filters) -> FIFO occupies MB 0..23         *
 *    private_can RFFN=6 (56 filters) -> FIFO occupies MB 0..19         *
 *                                                                     *
 *  MB 26..31 are reserved for TX on both channels and are polled       *
 *  round-robin (next available MB wins).  The cursor is per-channel   *
 *  so PUBLIC and PRIVATE never collide on the same hardware MB.       *
 * -------------------------------------------------------------------- */
#define CAN_TX_MB_FIRST  26u   /**< First TX mailbox (inclusive)        */
#define CAN_TX_MB_LAST   31u   /**< Last TX mailbox (inclusive)         */
#define CAN_TX_MB_COUNT   6u   /**< Number of TX mailboxes for round-robin */

/** Per-channel round-robin cursor into the 26..31 TX mailbox window. */
static u8 s_tx_cursor[CAN_CH_MAX] = { CAN_TX_MB_FIRST, CAN_TX_MB_FIRST };

/* Cooldown for the "all 6 TX MB busy" warning. The can_tx sweep
 * can legitimately hit the mailbox ceiling on cold boot while
 * draining a backlog of due frames; warn at most once per window
 * to keep the log readable. */
#define CAN_TX_BUSY_WARN_COOLDOWN_MS  200u
static u32 s_tx_busy_warn_ms[CAN_CH_MAX] = { 0u, 0u };

/**
 * @brief   Pick the next TX mailbox via round-robin
 * @brief   通过轮询选取下一个发送邮箱
 *
 * @details Walks MB 26..31 in order and returns the first one whose
 *          driver state is FLEXCAN_MB_IDLE (i.e. not currently
 *          transmitting).  When all six are busy the function
 *          returns 0xFFu so the caller can report ERR_BUSY.
 *
 *          The start cursor is the channel's last successful TX
 *          mailbox (s_tx_cursor[ch]) so that load spreads evenly
 *          and a mailbox that just finished is preferred for the
 *          next frame.
 *
 * @param[in]  inst  flexcan instance index (for status query)
 * @param[in]  ch    Logical channel (advances its cursor on success)
 *
 * @return  u8  Selected mailbox index (26..31), or 0xFFu if all busy
 */
static u8 prv_pick_tx_mb(u8 inst, can_channel_t ch)
{
    u8 start = s_tx_cursor[ch];
    for (u8 off = 0; off < CAN_TX_MB_COUNT; off++) {
        u8 mb = (u8)(CAN_TX_MB_FIRST +
                     ((start - CAN_TX_MB_FIRST + off) % CAN_TX_MB_COUNT));
        if (FLEXCAN_DRV_GetTransferStatus(inst, mb) != STATUS_BUSY) {
            s_tx_cursor[ch] = (u8)(mb + 1u);
            if (s_tx_cursor[ch] > CAN_TX_MB_LAST) {
                s_tx_cursor[ch] = CAN_TX_MB_FIRST;
            }
            return mb;
        }
    }
    return 0xFFu;  /* all six TX mailboxes busy */
}

/* -------------------------------------------------------------------- *
 *  Public API
 * -------------------------------------------------------------------- */

/**
 * @brief   Initialize both CAN channels and the RX ring buffer
 * @brief   初始化两条 CAN 通道及接收环形缓冲
 *
 * @details Initializes the ring buffer to empty, then calls
 *          FLEXCAN_DRV_Init + InstallEventCallback for each
 *          channel. On any failure the function returns early
 *          with C02B2_ERR and the partially-initialized channel
 *          is left in an undefined state.
 *
 * @return  c02b2_result_t
 * @retval  C02B2_OK  Both channels up
 * @retval  C02B2_ERR At least one FLEXCAN_DRV_Init failed
 */
void CanIf_InstallFlexcanCallbacks(void)
{
    for (u32 ch = 0; ch < CAN_CH_MAX; ch++) {
        const u8 inst = prv_logical_to_inst((can_channel_t)ch);
        FLEXCAN_DRV_InstallEventCallback(inst, prv_flexcan_cb, NULL);
        FLEXCAN_DRV_InstallErrorCallback(inst, prv_flexcan_err_cb, NULL);
    }
}

c02b2_result_t CanIf_ArmRxFifo(void)
{
    /* CRITICAL: in RX-FIFO mode, the SDK does NOT enable the
     * FRAME_AVAILABLE / WARNING / OVERFLOW interrupts during
     * FLEXCAN_DRV_Init.  The interrupts are enabled inside
     * FLEXCAN_StartRxMessageFifoData -- the function behind
     * FLEXCAN_DRV_RxFifo().  The first call therefore arms the
     * FIFO; subsequent calls inside the event callback re-arm it
     * for the next frame.  A missing first call leaves the
     * controller alive on the bus but with all 3 RX-FIFO interrupts
     * masked, which manifests as "the bus is up, the analyzer sends
     * frames, but the application never sees an RX callback". */
    for (u32 ch = 0; ch < CAN_CH_MAX; ch++) {
        const u8 inst = prv_logical_to_inst((can_channel_t)ch);
        const status_t r = FLEXCAN_DRV_RxFifo(inst, &s_rx_fifo_start_buf);
        if (r != STATUS_SUCCESS) {
            LOG_E("RxFifo prime failed inst=%u (%d)", (unsigned)inst, (int)r);
            return C02B2_ERR;
        }
    }
    return C02B2_OK;
}

c02b2_result_t CanIf_Init(void)
{
    /* All FlexCAN hardware bring-up (FLEXCAN_DRV_Init, FIFO filter
     * configuration, callback installation, FIFO priming) lives in
     * Can_Init().  This function is intentionally lightweight:
     * it just resets the application-layer RX ring buffer so the
     * scheduler tick has a known-empty queue at boot.  It is safe
     * to call more than once. */
    s_rx_ring.head = 0u;
    s_rx_ring.tail = 0u;
    if (s_if_inited) {
        LOG_I("init (re-entry, ring reset only)");
    } else {
        s_if_inited = true;
        LOG_I("init OK (ring buffer armed; HW up via Can_Init())");
    }
    return C02B2_OK;
}

/**
 * @brief   Register an RX callback (legacy stub)
 * @brief   注册一个 RX 回调（保留旧 API，无实际操作）
 *
 * @details No-op retained for source compatibility. The real
 *          routing table lives in can_db.
 *
 * @param[in]  ch     Logical channel
 * @param[in]  can_id CAN id (unused)
 * @param[in]  ide    0=STD, 1=EXT (unused)
 * @param[in]  cb     Callback (unused)
 *
 * @return  c02b2_result_t  Always C02B2_OK
 */
c02b2_result_t CanIf_RegisterRx(can_channel_t ch, u32 can_id, u8 ide, can_rx_cb_t cb)
{
    /* DEPRECATED stub. Retained for source compatibility with the
     * pre-DBC API; has no runtime effect. RX routing is driven by
     * can_db.c (CanDb_DispatchByDb) walking can_msg_descs_ipk[] on
     * the ring buffer the ISR fills. Future per-id filtering should
     * be expressed in the DBC + can_db_ipk_gen, not in this hook. */
    (void)ch; (void)can_id; (void)ide; (void)cb;
    return C02B2_OK;
}

/**
 * @brief   Send a single CAN frame on the chosen channel
 * @brief   在指定通道上发送一帧 CAN 报文
 *
 * @details Pack the portable can_msg_t into the vendor flexcan_msgbuff_t,
 *          then ask the driver to transmit on a reserved mailbox
 *          (MB 30 for PUBLIC, MB 31 for PRIVATE). If the MB is busy
 *          the frame is dropped (not queued) to keep the call
 *          non-blocking from the tick context.
 *
 * @param[in]  ch   Logical channel
 * @param[in]  msg  Frame to transmit (id/ide/rtr/dlc/data)
 *
 * @return  c02b2_result_t
 * @retval  C02B2_OK        Accepted by the driver
 * @retval  C02B2_ERR_BUSY  Mailbox busy (frame dropped)
 */
c02b2_result_t CanIf_Send(can_channel_t ch, const can_msg_t *msg)
{
    u8 inst = prv_logical_to_inst(ch);
    /* Zero-init every field up front; we then overwrite only the
     * fields the driver actually needs for classic CAN.  Without
     * this, fd_enable / fd_padding / enable_brs pick up stack
     * garbage and the driver puts CAN-FD frames on a classic-CAN
     * bus -- the analyzer sees nothing, the driver counts bit
     * errors, and we end up in a BUS_OFF / recover loop.
     * Designated-initializer used so the enum-typed fields stay
     * enum-typed (silences Pe188 that plain = { 0 } would
     * trigger: 0 is int, fields are flexcan_msg_id_type_t). */
    flexcan_data_info_t info = {
        .msg_id_type = FLEXCAN_MSG_ID_STD,
        .data_length = 0u,
        .fd_enable   = false,
        .fd_padding  = 0u,
        .enable_brs  = false,
        .is_remote   = false,
    };

    /* Select 11-bit / 29-bit id format. msg->ide is u8 to stay portable;
     * the comparison is wrapped in a conditional to silence Pe188 (enum
     * mixed with other type). The result is then assigned to the enum
     * field of info. */
    info.msg_id_type = (msg->ide != 0u) ? FLEXCAN_MSG_ID_EXT
                                        : FLEXCAN_MSG_ID_STD;
    info.data_length = msg->dlc;
    info.is_remote   = (msg->rtr != 0u);

    /* Round-robin pick from MB 26..31. prv_pick_tx_mb() returns
     * 0xFFu when all six are busy, in which case we report
     * C02B2_ERR_BUSY and let the caller retry next tick. */
    u8 mb_idx = prv_pick_tx_mb(inst, ch);
    if (mb_idx == 0xFFu) {
        /* Mailbox pool exhausted (cold-boot backlog, transient
         * bus saturation). Log at most once per cooldown window
         * per channel; can_tx will retry the deferred frame on
         * the next sweep. */
        const u32 now_ms = OSIF_GetMilliseconds();
        if ((now_ms - s_tx_busy_warn_ms[ch]) >= CAN_TX_BUSY_WARN_COOLDOWN_MS) {
            s_tx_busy_warn_ms[ch] = now_ms;
            LOG_W("send id=0x%X all 6 TX MB busy (suppressed %ums)",
                  (unsigned)msg->id, (unsigned)CAN_TX_BUSY_WARN_COOLDOWN_MS);
        }
        return C02B2_ERR_BUSY;
    }
    /* FLEXCAN_DRV_Send takes (inst, mb_idx, &info, msg_id, mb_data):
     * the driver fills the MB CS word (id type, dlc, remote bit)
     * from tx_info internally; no separate FLEXCAN_DRV_ConfigTxMb
     * call is required for one-shot sends. */
    status_t r = FLEXCAN_DRV_Send(inst, mb_idx, &info, msg->id, msg->data);
    if (r != STATUS_SUCCESS) {
        LOG_W("send id=0x%X failed (%d)", (unsigned)msg->id, (int)r);
        return C02B2_ERR_BUSY;
    }
    return C02B2_OK;
}

/**
 * @brief   Globally enable/disable TX on a channel (stub)
 * @brief   全局使能/禁止某通道的发送（占位）
 *
 * @param[in]  ch  Channel
 * @param[in]  en  true = enable, false = disable
 *
 * @return  c02b2_result_t  Always C02B2_OK
 */
c02b2_result_t CanIf_SetTxEnabled(can_channel_t ch, bool en)
{
    (void)ch; (void)en;
    /* For now TX is always enabled. Hook to CAN silence / NM later. */
    return C02B2_OK;
}

/**
 * @brief   Request the channel to enter sleep mode (stub)
 * @brief   请求通道进入休眠模式（占位）
 *
 * @param[in]  ch  Channel
 *
 * @return  c02b2_result_t  Always C02B2_OK
 */
c02b2_result_t CanIf_GoToSleep(can_channel_t ch)
{
    (void)ch;
    /* TODO: enter freeze mode + transceiver STBY pin. */
    return C02B2_OK;
}

/**
 * @brief   Wake the channel from sleep (stub)
 * @brief   将通道从休眠唤醒（占位）
 *
 * @param[in]  ch  Channel
 *
 * @return  c02b2_result_t  Always C02B2_OK
 */
c02b2_result_t CanIf_WakeUp(can_channel_t ch)
{
    (void)ch;
    /* TODO: exit freeze, mark wakeup event. */
    return C02B2_OK;
}

/**
 * @brief   Test whether the channel is in bus-off state (stub)
 * @brief   测试通道是否处于 bus-off 状态（占位）
 *
 * @param[in]  ch  Channel
 *
 * @return  bool
 * @retval  true   Bus-off recorded (stub: always false)
 * @retval  false  Not in bus-off
 */
bool CanIf_IsBusOff(can_channel_t ch)
{
    (void)ch;
    /* TODO: read CAN->ECR register (Error Counter Register). */
    return false;
}

/**
 * @brief   Get the cumulative bus-off recovery count (stub)
 * @brief   获取累计 bus-off 恢复次数（占位）
 *
 * @param[in]  ch  Channel
 *
 * @return  u32  Bus-off count (stub: always 0)
 */
u32 CanIf_GetBusOffCount(can_channel_t ch)
{
    (void)ch;
    return 0u;
}

/**
 * @brief   Pop one received frame from the ring
 * @brief   从环形缓冲弹出一帧接收报文
 *
 * @param[out] out  Populated on success
 *
 * @return  bool
 * @retval  true   Frame popped
 * @retval  false  Ring empty
 */
bool CanIf_PopRx(can_msg_t *out)
{
    return prv_ring_pop(out);
}

/**
 * @brief   Number of frames currently waiting in the RX ring
 * @brief   接收环中当前等待的报文数量
 *
 * @details Modulo arithmetic gives correct wrap-around for both
 *          head >= tail (normal) and head < tail (wrapped) cases.
 *          Adding CAN_RX_RING_SIZE before the modulo prevents
 *          underflow when head < tail.
 *
 * @return  u32  Count of pending frames
 */
u32 CanIf_RxPending(void)
{
    /* Snapshot both volatile indices to avoid Pa082. */
    u8 head = s_rx_ring.head;
    u8 tail = s_rx_ring.tail;
    /* +CAN_RX_RING_SIZE guards against negative intermediate when head < tail. */
    return (u32)((head + CAN_RX_RING_SIZE - tail) % CAN_RX_RING_SIZE);
}


/**
 * @brief   Return the post-FIFO mailbox layout for a channel
 * @brief   返回某通道 FIFO 配置之后的邮箱布局
 *
 * @details Hard-coded from board/can_config.c:
 *          - public_can  : FIFO 0..23 (72 filters),  RX 24..25,  TX 26..31
 *          - private_can : FIFO 0..19 (56 filters),  RX 20..25,  TX 26..31
 *
 * @param[in]  ch  Logical channel
 *
 * @return  can_mb_layout_t  RX/TX mailbox windows for the caller
 */
can_mb_layout_t CanIf_GetMbLayout(can_channel_t ch)
{
    can_mb_layout_t lay;
    if (ch == CAN_CH_PUBLIC) {
        /* public_can:  RFFN=8 (72 filters) -> FIFO MB 0..23 */
        lay.rx_mb_first = 24u;
        lay.rx_mb_last  = 25u;
        lay.tx_mb_first = CAN_TX_MB_FIRST;
        lay.tx_mb_last  = CAN_TX_MB_LAST;
    } else {
        /* private_can: RFFN=6 (56 filters) -> FIFO MB 0..19 */
        lay.rx_mb_first = 20u;
        lay.rx_mb_last  = 25u;
        lay.tx_mb_first = CAN_TX_MB_FIRST;
        lay.tx_mb_last  = CAN_TX_MB_LAST;
    }
    return lay;
}

/**
 * @brief   Configure a single RX mailbox with an exact CAN id
 * @brief   把单个接收邮箱配置为精确匹配某个 CAN id
 *
 * @details Wraps FLEXCAN_DRV_ConfigRxMb so application code does not
 *          have to know the post-FIFO mailbox window.  The mailbox
 *          index must be inside the channel's rx_mb_first..rx_mb_last
 *          range returned by CanIf_GetMbLayout(); anything else is
 *          rejected to avoid stomping on the FIFO ID filter table
 *          or the TX round-robin pool.
 *
 * @param[in]  ch      Logical channel
 * @param[in]  mb_idx  Mailbox index
 * @param[in]  can_id  11-bit standard id to match
 * @param[in]  ide     0 = STD, 1 = EXT
 *
 * @return  c02b2_result_t
 * @retval  C02B2_OK         Mailbox configured and armed for RX
 * @retval  C02B2_ERR_PARAM  mb_idx outside the allowed RX window or
 *                           the SDK rejected the configuration
 */
c02b2_result_t CanIf_ConfigRxMb(can_channel_t ch, u8 mb_idx,
                                u32 can_id, u8 ide)
{
    const can_mb_layout_t lay = CanIf_GetMbLayout(ch);
    if (mb_idx < lay.rx_mb_first || mb_idx > lay.rx_mb_last) {
        LOG_W("ConfigRxMb: mb=%u outside RX window [%u..%u] on ch=%u",
              (unsigned)mb_idx,
              (unsigned)lay.rx_mb_first, (unsigned)lay.rx_mb_last,
              (unsigned)ch);
        return C02B2_ERR_PARAM;
    }
    const u8 inst = prv_logical_to_inst(ch);
    /* Designated-init to silence Pe188 (0-as-int into enum fields)
     * while keeping every boolean field explicitly false.  Same
     * defensive style as CanIf_Send: never trust stack garbage
     * on boolean driver flags. */
    flexcan_data_info_t info = {
        .msg_id_type = FLEXCAN_MSG_ID_STD,
        .data_length = 8u,
        .fd_enable   = false,
        .fd_padding  = 0u,
        .enable_brs  = false,
        .is_remote   = false,
    };
    info.msg_id_type = (ide != 0u) ? FLEXCAN_MSG_ID_EXT : FLEXCAN_MSG_ID_STD;
    if (FLEXCAN_DRV_ConfigRxMb(inst, mb_idx, &info, can_id) != STATUS_SUCCESS) {
        LOG_E("ConfigRxMb failed inst=%u mb=%u id=0x%X",
              (unsigned)inst, (unsigned)mb_idx, (unsigned)can_id);
        return C02B2_ERR;
    }
    /* Per YTM32B1M init flow: FLEXCAN_EnableRxFifo writes RXIMR[i] = 0
     * for every MB outside the FIFO table.  That zeroes the per-MB ID
     * mask, so any frame reaching this MB is filtered out before the
     * CS-word ID compare runs.  ConfigRxMb itself does NOT touch
     * RXIMR - we have to open the mask explicitly.  Use "all-ones" for
     * exact-match (every ID bit must match the CS-word ID).  Set
     * SetRxMaskType stays at the default GLOBAL: all MB indices
     * 20..25 are >= 14 so they always consult RXIMR regardless of MRP,
     * and SetRxIndividualMask correctly distinguishes STD vs EXT in
     * the bit positions the compare logic cares about. */
    const flexcan_msgbuff_id_type_t id_type = (ide != 0u) ? FLEXCAN_MSG_ID_EXT
                                                          : FLEXCAN_MSG_ID_STD;
    const uint32_t all_ones = (ide != 0u) ? 0x1FFFFFFFu : 0x7FFu;
    if (FLEXCAN_DRV_SetRxIndividualMask(inst, id_type, mb_idx, all_ones)
            != STATUS_SUCCESS) {
        LOG_E("ConfigRxMb mask set failed inst=%u mb=%u",
              (unsigned)inst, (unsigned)mb_idx);
        return C02B2_ERR;
    }
    /* Arm the MB so the driver writes into it on the next match. */
    if (FLEXCAN_DRV_Receive(inst, mb_idx, &s_rx_arm_buf) != STATUS_SUCCESS) {
        LOG_W("ConfigRxMb arm failed inst=%u mb=%u", (unsigned)inst, (unsigned)mb_idx);
    }
    LOG_I("ConfigRxMb ch=%u mb=%u id=0x%X ide=%u",
          (unsigned)ch, (unsigned)mb_idx, (unsigned)can_id, (unsigned)ide);
    return C02B2_OK;
}




/* ---------------------------------------------------------------- *
 *  DBC-aware convenience wrappers (see can_if.h for contract)
 *
 *  These forward to app/can/can_tx.c + can_rx.c so the caller
 *  (typically mod_can_demo or a diag module) only needs to include
 *  can_if.h.
 *
 *  The forward declarations below let us avoid pulling
 *  app/can/can_tx.h + can_rx.h into can_if.c -- the linker resolves
 *  them at link time.  Keep the signatures in lock-step with the
 *  real definitions.
 * ---------------------------------------------------------------- */
c02b2_result_t CanTx_PreparePayload(u32 can_id, const u8 *data, u8 dlc);
c02b2_result_t CanTx_EncodeSignal(u32 can_id, u16 sig_id, u32 raw);
c02b2_result_t CanTx_Trigger(u32 can_id);
c02b2_result_t CanRx_GetLastRawFrame(u32 can_id, can_msg_t *out);
u32 CanRx_GetRawFrameCount(void);

c02b2_result_t CanIf_TxPreparePayload(u32 can_id, const u8 *data, u8 dlc)
{
    return CanTx_PreparePayload(can_id, data, dlc);
}

c02b2_result_t CanIf_TxEncodeSignal(u32 can_id, u16 sig_id, u32 raw)
{
    return CanTx_EncodeSignal(can_id, sig_id, raw);
}

c02b2_result_t CanIf_TxTrigger(u32 can_id)
{
    return CanTx_Trigger(can_id);
}

c02b2_result_t CanIf_RxGetLastRawFrame(u32 can_id, can_msg_t *out)
{
    return CanRx_GetLastRawFrame(can_id, out);
}

u32 CanIf_RxGetRawFrameCount(void)
{
    return CanRx_GetRawFrameCount();
}

/* ---------------------------------------------------------------- *
 *  Module-level bring-up (formerly app/drv_api/can/can_init.c)
 *
 *  Can_Init() drives the FlexCAN hardware bring-up in a single
 *  place so app/main.c / app/init/drv_init.c do not have to know
 *  about the SDK call sequence.
 *
 *  Bring-up sequence:
 *    1. FLEXCAN_DRV_Init              - reset MCR / RAM / masks
 *    2. FLEXCAN_DRV_ConfigRxFifo      - write ID filter tables
 *    3. CanIf_InstallFlexcanCallbacks - subscribe RX/TX/error events
 *    4. CanIf_ArmRxFifo              - enable FRAME_AVAILABLE/WARN/
 *                                       OVERFLOW IRQs, arm first RX
 * ---------------------------------------------------------------- */

/* Legacy FIFO ID filter table lives in FlexCAN RAM at offset 0x18
 * (RxFifoFilterTableOffset). Each element is 8 bytes; the table has
 * `RxFifoFilterElementNum(RFFN) = (RFFN + 1) * 8` elements, which is
 * exactly num_id_filters from board/can_config.c.
 *
 * Format A is used (one full 29-bit ID + RTR + IDE flags per element).
 * Combined with the default RXFGMASK = 0xFFFFFFFF and RXIMR[i] = 0xFFFFFFFF
 * (set by FLEXCAN_Init), the FIFO performs a strict-match accept
 * against the table IDs (mask all ones -> incoming ID must equal the
 * table ID).
 *
 * Arrays are plain statics (not const) so FLEXCAN_DRV_ConfigRxFifo can
 * iterate without tripping the volatile qualifier. */
#define CAN_RX_FILTER_ELEMS_PUBLIC   72u   /* RFFN=8 -> 8*9 = 72 */
#define CAN_RX_FILTER_ELEMS_PRIVATE  56u   /* RFFN=6 -> 8*7 = 56 */

static flexcan_id_table_t s_rx_filter_public[CAN_RX_FILTER_ELEMS_PUBLIC];
static flexcan_id_table_t s_rx_filter_private[CAN_RX_FILTER_ELEMS_PRIVATE];

/** Populate s_rx_filter_public[] from can_msg_descs_ipk[].
 *  Each non-TX entry is copied into a free slot of the FIFO ID filter
 *  table (FORMAT_A). Standard frames only, RTR=0, IDE=0. Unused slots
 *  remain all-zero (which would match ID=0 with the default
 *  RXFGMASK=0xFFFFFFFF mask). */
static void prv_fill_filter_public(void)
{
    u32 n = 0u;
    for (u32 i = 0u; i < (u32)CAN_DB_IPK_RX_COUNT; i++) {
        const can_msg_desc_t *d = &can_msg_descs_ipk[i];
        if (d->is_tx != 0u) { continue; }   /* RX only */
        if (n >= (u32)CAN_RX_FILTER_ELEMS_PUBLIC) { break; }
        s_rx_filter_public[n].id              = d->can_id;
        s_rx_filter_public[n].isRemoteFrame   = false;
        s_rx_filter_public[n].isExtendedFrame = false;
        n++;
    }
}

/**
 * @brief   Initialize both FlexCAN instances
 * @brief   初始化两个 FlexCAN 实例
 *
 * @details Two CAN channels are wired on this board:
 *   - instance 1 = private bus (body / chassis domain, off-board sensors)
 *   - instance 2 = public bus  (powertrain / infotainment, OBD link)
 *
 *          Both stay in FLEXCAN_NORMAL_MODE (no loopback) so the
 *          cluster can really talk to the rest of the car.
 */
void Can_Init(void)
{
    /* 1. Controller init -- resets MCR / RAM / masks, sets RFFN. */
    FLEXCAN_DRV_Init(1, &private_can_State, &private_can);
    FLEXCAN_DRV_Init(2, &public_can_State,  &public_can);

    /* 2. Write ID filter tables. Without this step the Legacy FIFO
     * filter table sits at boot-time residual values and incoming
     * frames that do not match are silently dropped (IFLAG1 never
     * sets, no RX callback fires). */
    prv_fill_filter_public();
    (void)FLEXCAN_DRV_ConfigRxFifo(1u, FLEXCAN_RX_FIFO_ID_FORMAT_A,
                                   s_rx_filter_private);
    (void)FLEXCAN_DRV_ConfigRxFifo(2u, FLEXCAN_RX_FIFO_ID_FORMAT_A,
                                   s_rx_filter_public);

    /* 3. Subscribe to driver events (RX / TX / error callbacks). */
    CanIf_InstallFlexcanCallbacks();

    /* 4. Prime the RX FIFO -- this is the call that actually enables
     * the FRAME_AVAILABLE / WARNING / OVERFLOW interrupts in RX-FIFO
     * mode. Without it, even a perfectly configured filter produces
     * no IRQs. */
    (void)CanIf_ArmRxFifo();
}
