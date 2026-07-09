/**
 * @file    can_init.c
 * @brief   Initialize FlexCAN instances 1 (private) and 2 (public)
 *
 * @details Two CAN channels are wired on this board:
 *   - instance 1 = private bus (body / chassis domain, off-board sensors)
 *   - instance 2 = public bus (powertrain / infotainment, on the OBD link)
 *
 * Both stay in FLEXCAN_NORMAL_MODE (no loopback) so the cluster can
 * really talk to the rest of the car. Drivers register their user
 * callbacks via CanIf_InstallEventCallback (called from CanIf_Init in
 * main.c, AFTER this function returns).
 */
#include "sdk_project_config.h"
#include "YTM32B1MD1.h"
#include "flexcan_driver.h"
#include "can_db.h"              /* extern can_msg_descs_ipk[]                   */
#include "can_db_ipk_gen.h"      /* CAN_DB_IPK_MSG_COUNT / _RX_COUNT etc.       */
#include "can_if.h"             /* CanIf_InstallFlexcanCallbacks / ArmRxFifo  */

/* -------------------------------------------------------------------- *
 *  RX FIFO ID filter tables
 *
 *  Legacy FIFO ID filter table lives in FlexCAN RAM at offset 0x18
 *  (RxFifoFilterTableOffset). Each element is 8 bytes; the table has
 *  `RxFifoFilterElementNum(RFFN) = (RFFN + 1) * 8` elements, which is
 *  exactly num_id_filters from board/can_config.c.
 *
 *  Format A is used (one full 29-bit ID + RTR + IDE flags per
 *  element).  Combined with the default RXFGMASK = 0xFFFFFFFF
 *  and RXIMR[i] = 0xFFFFFFFF (set by FLEXCAN_Init), the FIFO performs
 *  a strict-match accept against the table IDs (mask all ones ->
 *  incoming ID must equal the table ID).
 *
 *  Arrays are plain statics (not const) so FLEXCAN_DRV_ConfigRxFifo can
 *  iterate without tripping the volatile qualifier.
 * -------------------------------------------------------------------- */
#define CAN_RX_FILTER_ELEMS_PUBLIC   72u   /* RFFN=8 -> 8*9 = 72 */
#define CAN_RX_FILTER_ELEMS_PRIVATE  56u   /* RFFN=6 -> 8*7 = 56 */

static flexcan_id_table_t s_rx_filter_public[CAN_RX_FILTER_ELEMS_PUBLIC];
static flexcan_id_table_t s_rx_filter_private[CAN_RX_FILTER_ELEMS_PRIVATE];

/**
 * @brief   Populate s_rx_filter_public[] from can_msg_descs_ipk[].
 * @brief   从 can_msg_descs_ipk[] 填充 s_rx_filter_public[]
 *
 * @details Each non-TX entry in can_msg_descs_ipk[] is copied into a
          free slot of the FIFO ID filter table (FORMAT_A).  Standard
          frames only, RTR=0, IDE=0.  Unused slots remain all-zero (which
          would match ID=0 with the default RXFGMASK=0xFFFFFFFF mask).
 */
static void prv_fill_filter_public(void)
{
    u32 n = 0u;
    for (u32 i = 0u; i < (u32)CAN_DB_IPK_MSG_COUNT; i++) {
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
 *
 * @details Two CAN channels are wired on this board:
 *   - instance 1 = private bus (body / chassis domain, off-board sensors)
 *   - instance 2 = public bus (powertrain / infotainment, on the OBD link)
 *
 * Both stay in FLEXCAN_NORMAL_MODE (no loopback) so the cluster can
 * really talk to the rest of the car.
 *
 * Bring-up sequence (kept in one place per project convention):
 *   1. FLEXCAN_DRV_Init              - reset controller, clear RAM, enable FIFO
 *   2. FLEXCAN_DRV_ConfigRxFifo      - write ID filter tables to FlexCAN RAM
 *   3. CanIf_InstallFlexcanCallbacks - subscribe to RX/TX/error events
 *   4. CanIf_ArmRxFifo              - enable FRAME_AVAILABLE / WARNING /
 *                                    OVERFLOW IRQs and arm the first receive
 *
 * Steps 2 and 3 must happen between Init and the first RxFifo arm; doing
 * them out of order leaves the bus alive but with no usable RX path.
 */
void Can_Init(void)
{
    /* 1. Controller init -- resets MCR / RAM / masks, sets RFFN. */
    FLEXCAN_DRV_Init(1, &private_can_State, &private_can);
    FLEXCAN_DRV_Init(2, &public_can_State, &public_can);

    /* 2. Write ID filter tables.  Without this step the Legacy FIFO
     * filter table sits at boot-time residual values and incoming
     * frames that do not match are silently dropped (IFLAG1 never
     * sets, no RX callback fires). */
    prv_fill_filter_public();
    (void)FLEXCAN_DRV_ConfigRxFifo(1u,
                                  FLEXCAN_RX_FIFO_ID_FORMAT_A,
                                  s_rx_filter_private);
    (void)FLEXCAN_DRV_ConfigRxFifo(2u,
                                  FLEXCAN_RX_FIFO_ID_FORMAT_A,
                                  s_rx_filter_public);

    /* 3. Subscribe to driver events (RX / TX / error callbacks). */
    CanIf_InstallFlexcanCallbacks();

    /* 4. Prime the RX FIFO -- this is the call that actually enables the
     * FRAME_AVAILABLE / WARNING / OVERFLOW interrupts in RX-FIFO mode.
     * Without it, even a perfectly configured filter produces no IRQs. */
    (void)CanIf_ArmRxFifo();
}
