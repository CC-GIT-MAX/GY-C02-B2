/**
 * @file    mod_can_demo.h
 * @brief   CAN send/receive demo module (NORMAL mode + external CAN tool)
 *
 * Goals:
 *   - Keep board/can_config.c unchanged (both channels in NORMAL mode).
 *   - TX: every 1s, push an 8-byte counter into 0x0260 (IPK_SettingRequest)
 *         via CanTx_PreparePayload so the can_tx 10ms tick forwards it to
 *         CanIf_Send -> FLEXCAN_DRV_Send.
 *   - RX: every 100ms, drain CanIf_PopRx and LOG id + first 2 bytes, so
 *         an external CAN tool (CANalyzer / CANcase / VN1610 ...) sending
 *         any DBC-known can_id is observable in the log.
 *
 * Without an external bus:
 *   - The driver will report BUSY because the bus is not ACK'd; the log
 *     shows the can_tx tick tried to send and the driver rejected.
 * With an external bus:
 *   - Upstream frames are decoded by can_rx (DBC dispatch) and any frame
 *     (known or not) is also logged by mod_can_demo's RX drain.
 */
#ifndef C02B2_MOD_CAN_DEMO_H
#define C02B2_MOD_CAN_DEMO_H

#include "scheduler.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief  Module descriptor (registered in scheduler.c). */
extern const mod_desc_t mod_can_demo;

#ifdef __cplusplus
}
#endif

#endif /* C02B2_MOD_CAN_DEMO_H */
