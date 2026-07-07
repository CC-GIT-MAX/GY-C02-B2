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

/** @brief  Get the flexcan state object for a channel. */
static flexcan_state_t *prv_state(can_channel_t ch)
{
    return (ch == CAN_CH_PUBLIC) ? &public_can_State : &private_can_State;
}

/** @brief  Get the flexcan user config for a channel. */
static const flexcan_user_config_t *prv_cfg(can_channel_t ch)
{
    return (ch == CAN_CH_PUBLIC) ? &public_can : &private_can;
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
    /* Only handle RX-FIFO completion; TX-complete / error handled separately. */
    if (ev == FLEXCAN_EVENT_RXFIFO_COMPLETE) {
        flexcan_msgbuff_t mb;
        /* Pull raw frame from hardware mailbox. */
        if (FLEXCAN_DRV_Receive(instance, (u8)buffIdx, &mb) == STATUS_SUCCESS) {
            can_msg_t m;
            /* Convert vendor format to portable can_msg_t. */
            prv_extract_msg(&mb, &m);
            /* Push to ring; on overflow drop silently (no blocking in ISR). */
            (void)prv_ring_push(&m);
        }
    }
    /* TX-complete / error events can be hooked here later. */
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
c02b2_result_t CanIf_Init(void)
{
    /* Reset ring buffer (empty state: head == tail == 0). */
    s_rx_ring.head = 0u;
    s_rx_ring.tail = 0u;

    /* Initialize every logical channel. */
    for (u32 ch = 0; ch < CAN_CH_MAX; ch++) {
        u8 inst = prv_logical_to_inst((can_channel_t)ch);
        flexcan_state_t *st = prv_state((can_channel_t)ch);
        const flexcan_user_config_t *cfg = prv_cfg((can_channel_t)ch);

        /* Init hardware; if any channel fails we abort immediately. */
        if (FLEXCAN_DRV_Init(inst, st, cfg) != STATUS_SUCCESS) {
            LOG_E("init inst=%u failed", (unsigned)inst);
            return C02B2_ERR;
        }
        /* Register our ISR callback for RX events. */
        FLEXCAN_DRV_InstallEventCallback(inst, prv_flexcan_cb, NULL);
    }
    LOG_I("init OK");
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
    /* Static table handles registration. Route through can_db instead. */
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
    flexcan_data_info_t info;

    /* Select 11-bit / 29-bit id format. msg->ide is u8 to stay portable;
     * the comparison is wrapped in a conditional to silence Pe188 (enum
     * mixed with other type). The result is then assigned to the enum
     * field of info. */
    info.msg_id_type = (msg->ide != 0u) ? FLEXCAN_MSG_ID_EXT
                                        : FLEXCAN_MSG_ID_STD;
    info.data_length = msg->dlc;
    info.is_remote   = (msg->rtr != 0u);

    /* Reuse MB 30 (public) / 31 (private) as TX slots. The YTM32B1M SDK
     * FLEXCAN_DRV_Send takes (inst, mb_idx, &info, msg_id, mb_data) -
     * five arguments with mb_data as const uint8_t*, NOT a flexcan_msgbuff_t.
     * The driver fills the MB CS word (id type, dlc, remote bit) from
     * tx_info internally; no separate FLEXCAN_DRV_ConfigTxMb call is
     * required for one-shot sends. */
    u8 mb_idx = (ch == CAN_CH_PUBLIC) ? 30u : 31u;
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
