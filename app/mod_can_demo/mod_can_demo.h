/**
 * @file    mod_can_demo.h
 * @brief   Demo / bring-up module that exercises the full CAN stack
 * @brief   演练完整 CAN 栈的 demo / 联调模块
 *
 * @details Exercises every layer end-to-end on a 1 s cadence:
          - signal-bus read   (Signal_Get on a few IPK signals)
          - timeout-bitmap    (decode SIG_CAN_RX_TIMEOUT_MAP bits back to CAN id)
          - raw frame cache   (CanRx_GetLastRawFrame for a chosen ID)
          - TX whole payload  (CanTx_PreparePayload + CanTx_Trigger)
          - TX one signal     (CanTx_EncodeSignal + CanTx_Trigger)
 *
 * The TX ids are from the IPK DBC only (CAN_DB_IPK_TX_COUNT entries);
 * the raw read is one of the IPK RX ids.  No full DBC import is needed
 * to run this.
 */
#ifndef C02B2_MOD_CAN_DEMO_H
#define C02B2_MOD_CAN_DEMO_H

#include "scheduler.h"

/**
 * @brief   Module descriptor for mod_can_demo (registered in scheduler.c)
 * @brief   mod_can_demo 的模块描述符（在 scheduler.c 中注册）
 */
extern const mod_desc_t mod_can_demo;

#endif /* C02B2_MOD_CAN_DEMO_H */
