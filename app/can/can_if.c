/**
 * @file    can_if.c
 * @brief   CAN interface implementation
 *
 * Bridges the app/can/* layer to the vendor flexcan_driver.
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
#include "can_db.h"
#include "power.h"

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
    volatile u8 head;   /* producer (ISR) */
    volatile u8 tail;   /* consumer (tick) */
} can_rx_ring_t;

static can_rx_ring_t s_rx_ring;

/* Single-producer / single-consumer lock-free ring */
static bool prv_ring_push(const can_msg_t *m)
{
    u8 next = (u8)((s_rx_ring.head + 1u) % CAN_RX_RING_SIZE);
    if (next == s_rx_ring.tail) {
        return false;  /* full */
    }
    s_rx_ring.slot[s_rx_ring.head] = *m;
    s_rx_ring.head = next;
    return true;
}

static bool prv_ring_pop(can_msg_t *m)
{
    if (s_rx_ring.head == s_rx_ring.tail) {
        return false;  /* empty */
    }
    *m = s_rx_ring.slot[s_rx_ring.tail];
    s_rx_ring.tail = (u8)((s_rx_ring.tail + 1u) % CAN_RX_RING_SIZE);
    return true;
}

/* -------------------------------------------------------------------- *
 *  flexcan glue
 * -------------------------------------------------------------------- */
static void prv_extract_msg(const flexcan_msgbuff_t *mb, can_msg_t *out)
{
    u32 cs = mb->cs;
    u8  ide = (u8)((cs & CAN_CS_IDE_MASK) ? 1u : 0u);
    u8  rtr = (u8)((cs & CAN_CS_RTR_MASK) ? 1u : 0u);
    u32 dlc = (cs & CAN_CS_DLC_MASK) >> CAN_CS_DLC_SHIFT;
    if (dlc > 8u) dlc = 8u;

    out->id  = (ide ? (mb->msgId & CAN_MSG_ID_EXT_MASK)
                    : (mb->msgId & CAN_MSG_ID_STD_MASK));
    out->ide = ide;
    out->rtr = rtr;
    out->dlc = (u8)dlc;
    for (u32 i = 0; i < dlc; i++) {
        out->data[i] = mb->data[i];
    }
    for (u32 i = dlc; i < 8u; i++) {
        out->data[i] = 0u;
    }
}

static u8 prv_logical_to_inst(can_channel_t ch)
{
    /* Match board/can_config.h naming */
    return (ch == CAN_CH_PUBLIC) ? public_can_INST : private_can_INST;
}

static flexcan_state_t *prv_state(can_channel_t ch)
{
    return (ch == CAN_CH_PUBLIC) ? &public_can_State : &private_can_State;
}

static const flexcan_user_config_t *prv_cfg(can_channel_t ch)
{
    return (ch == CAN_CH_PUBLIC) ? &public_can : &private_can;
}

/* Vendor callback runs in ISR context */
static void prv_flexcan_cb(u8 instance,
                           flexcan_event_type_t ev,
                           u32 buffIdx,
                           flexcan_state_t *state)
{
    (void)state;
    if (ev == FLEXCAN_EVENT_RXFIFO_COMPLETE) {
        flexcan_msgbuff_t mb;
        if (FLEXCAN_DRV_Receive(instance, (u8)buffIdx, &mb) == STATUS_SUCCESS) {
            can_msg_t m;
            prv_extract_msg(&mb, &m);
            (void)prv_ring_push(&m);  /* drop on overflow, ISR cannot block */
        }
    }
    /* TX-complete / error events can be hooked here later */
}

/* -------------------------------------------------------------------- *
 *  Public API
 * -------------------------------------------------------------------- */
lbx_result_t CanIf_Init(void)
{
    s_rx_ring.head = 0u;
    s_rx_ring.tail = 0u;

    for (u32 ch = 0; ch < CAN_CH_MAX; ch++) {
        u8 inst = prv_logical_to_inst((can_channel_t)ch);
        flexcan_state_t *st = prv_state((can_channel_t)ch);
        const flexcan_user_config_t *cfg = prv_cfg((can_channel_t)ch);

        if (FLEXCAN_DRV_Init(inst, st, cfg) != STATUS_SUCCESS) {
            LOG_E("init inst=%u failed", (unsigned)inst);
            return LBX_ERR;
        }
        FLEXCAN_DRV_InstallEventCallback(inst, prv_flexcan_cb, NULL);
    }
    CanDb_LogOnInit();
    LOG_I("init OK");
    return LBX_OK;
}

lbx_result_t CanIf_RegisterRx(can_channel_t ch, u32 can_id, u8 ide, can_rx_cb_t cb)
{
    /* Static table handles registration.  Route through can_db instead. */
    (void)ch; (void)can_id; (void)ide; (void)cb;
    return LBX_OK;
}

lbx_result_t CanIf_Send(can_channel_t ch, const can_msg_t *m)
{
    u8 inst = prv_logical_to_inst(ch);
    flexcan_msgbuff_t mb = {0};
    flexcan_data_info_t info = {0};

    info.msg_id_type  = m->ide ? FLEXCAN_MSG_ID_EXT : FLEXCAN_MSG_ID_STD;
    info.data_length  = m->dlc;
    info.is_remote    = (m->rtr != 0u);

    mb.msgId = m->id;
    for (u32 i = 0; i < m->dlc; i++) {
        mb.data[i] = m->data[i];
    }
    mb.dataLen = m->dlc;

    /* Use MB 0..N for TX.  For simplicity, allocate per-Send a fresh MB. */
    /* Reuse MB 30/31 as TX slots; if busy, drop. */
    u8 mb_idx = (ch == CAN_CH_PUBLIC) ? 30u : 31u;
    status_t r = FLEXCAN_DRV_Send(inst, mb_idx, &info, &mb);
    if (r != STATUS_SUCCESS) {
        LOG_W("send id=0x%X failed (%d)", (unsigned)m->id, (int)r);
        return LBX_ERR_BUSY;
    }
    return LBX_OK;
}

lbx_result_t CanIf_SetTxEnabled(can_channel_t ch, bool en)
{
    (void)ch; (void)en;
    /* For now TX is always enabled.  Hook to CAN silence / NM later. */
    return LBX_OK;
}

lbx_result_t CanIf_GoToSleep(can_channel_t ch)
{
    (void)ch;
    /* TODO: enter freeze mode + transceiver STBY */
    return LBX_OK;
}

lbx_result_t CanIf_WakeUp(can_channel_t ch)
{
    (void)ch;
    /* TODO: exit freeze, mark wakeup event */
    return LBX_OK;
}

bool CanIf_IsBusOff(can_channel_t ch)
{
    (void)ch;
    /* TODO: read CAN->ECR register */
    return false;
}

u32 CanIf_GetBusOffCount(can_channel_t ch)
{
    (void)ch;
    return 0u;
}

/* Pop one frame from the ring (called by can_rx in tick context) */
bool CanIf_PopRx(can_msg_t *out)
{
    return prv_ring_pop(out);
}

/* Bytes waiting in ring (for diagnostics) */
u32 CanIf_RxPending(void)
{
    return (u32)((s_rx_ring.head + CAN_RX_RING_SIZE - s_rx_ring.tail) % CAN_RX_RING_SIZE);
}
