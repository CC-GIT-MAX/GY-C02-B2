/**
 * @file    can_tx.h
 * @brief   CAN transmit module
 */
#ifndef LBX_MOD_CAN_TX_H
#define LBX_MOD_CAN_TX_H

#include "scheduler.h"
#include "result.h"
#include "types.h"

/**
 * @brief   Module descriptor for mod_can_tx (registered in scheduler.c)
 * @brief   mod_can_tx 的模块描述符（在 scheduler.c 中注册）
 */
extern const mod_desc_t mod_can_tx;

/**
 * @brief   Force immediate send of a single frame (event-driven)
 * @brief   强制立即发送一帧（事件驱动）
 *
 * @details Marks the matching entry in g_can_tx_db[] as pending
 *          so that the next 10 ms tick sends it regardless of
 *          its cycle. Used for one-shot responses to diag
 *          requests, button events, etc.
 *
 * @param[in]  can_id  11/29-bit CAN identifier (must match a db entry)
 *
 * @return  lbx_result_t
 * @retval  LBX_OK         Marked as pending
 * @retval  LBX_ERR_PARAM  No matching entry in g_can_tx_db[]
 */
lbx_result_t CanTx_Trigger(u32 can_id);

#endif /* LBX_MOD_CAN_TX_H */
