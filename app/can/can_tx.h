/**
 * @file    can_tx.h
 * @brief   CAN transmit module (DBC-driven)
 * @brief   CAN 发送模块（DBC 驱动）
 *
 * Two ways to populate a TX frame's payload:
 *
 *   1. Raw:  CanTx_PreparePayload(can_id, data, dlc)
 *      Build the entire 8-byte payload yourself (e.g. by calling
 *      CanDb_EncodeAndPack() for every signal) and hand it over.
 *
 *   2. Per-signal: CanTx_EncodeSignal(can_id, sig_id, value)
 *      Update ONE signal's bit field, leaving all other bits
 *      untouched.  Internally the function reads the message
 *      descriptor, finds the signal descriptor, and calls
 *      CanDb_EncodeAndPack() on the existing payload buffer.
 *
 * The 10 ms tick in mod_can_tx sends whatever is in the buffer.
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
 * @brief   Fill the payload buffer for a TX message (whole-buffer).
 * @brief   填充某条 TX 报文的 payload 缓冲（整报文）
 *
 * @param[in]  can_id  IPK message can_id
 * @param[in]  data    Source buffer (at least `dlc` bytes)
 * @param[in]  dlc     Data length (0..8)
 *
 * @return  lbx_result_t
 * @retval  LBX_OK         Payload queued
 * @retval  LBX_ERR_PARAM  can_id not an IPK TX message, or data NULL
 */
lbx_result_t CanTx_PreparePayload(u32 can_id, const u8 *data, u8 dlc);

/**
 * @brief   Update a single signal inside an existing TX payload.
 * @brief   在已有 TX payload 中更新单个信号
 *
 * @details Other signals in the same message keep their previous
 *          values.  This is the common call from business modules:
 *          "I just computed a new fuel-level value, please update
 *          the IPK_FuelLevelSts bit field for me."
 *
 * @param[in]  can_id  IPK TX message can_id
 * @param[in]  sig_id  CAN_DB_SIG_* signal id belonging to that message
 * @param[in]  value   Physical value (will be quantised per factor/offset)
 *
 * @return  lbx_result_t
 * @retval  LBX_OK         Signal packed into the slot's payload
 * @retval  LBX_ERR_PARAM  can_id not a TX message, sig_id not in it,
 *                         or value out of representable range
 */
lbx_result_t CanTx_EncodeSignal(u32 can_id, u16 sig_id, s32 value);

/**
 * @brief   Rebuild a TX payload from every signal of that message,
 *          reading current signal-bus values via Signal_Get().
 * @brief   从 signal bus 重新读所有信号, 全量重建一条 TX 报文的 payload
 *
 * @details Convenient for cyclic frames whose payload is fully
 *          derived from bus state (e.g. IPK_STS).  Triggered
 *          explicitly by the business module or by the rebuild
 *          policy in mod_can_tx::prv_tick.
 *
 * @param[in]  can_id  IPK TX message can_id
 *
 * @return  lbx_result_t
 * @retval  LBX_OK         Payload fully rebuilt
 * @retval  LBX_ERR_PARAM  can_id not a TX message
 */
lbx_result_t CanTx_RebuildFromSignals(u32 can_id);

/**
 * @brief   Override the cyclic period for a TX message.
 * @brief   覆盖某条 TX 报文的发送周期
 *
 * @param[in]  can_id     IPK TX message can_id
 * @param[in]  cycle_ms   Period in ms (0 = event-driven only)
 *
 * @return  lbx_result_t
 * @retval  LBX_OK        Period set
 * @retval  LBX_ERR_PARAM can_id not an IPK TX message
 */
lbx_result_t CanTx_SetCycle(u32 can_id, u16 cycle_ms);

/**
 * @brief   Force immediate send of a single TX frame (event-driven)
 * @brief   强制立即发送一帧（事件驱动）
 *
 * @param[in]  can_id  11-bit CAN identifier (must match a TX db entry)
 *
 * @return  lbx_result_t
 * @retval  LBX_OK         Marked as pending
 * @retval  LBX_ERR_PARAM  No matching TX entry
 */
lbx_result_t CanTx_Trigger(u32 can_id);

#endif /* LBX_MOD_CAN_TX_H */