/**
 * @file    can_tx.h
 * @brief   CAN transmit module
 */
#ifndef LBX_MOD_CAN_TX_H
#define LBX_MOD_CAN_TX_H

#include "scheduler.h"
#include "result.h"
#include "types.h"

extern const mod_desc_t mod_can_tx;

/* Force immediate send of a single frame (event-driven) */
lbx_result_t CanTx_Trigger(u32 can_id);

#endif /* LBX_MOD_CAN_TX_H */
