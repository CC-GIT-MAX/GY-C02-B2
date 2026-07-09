/**
 * @file    can_rx.h
 * @brief   CAN receive dispatcher module
 */
#ifndef C02B2_MOD_CAN_RX_H
#define C02B2_MOD_CAN_RX_H

#include "scheduler.h"
#include "drv_api/can/can_if.h"   /* can_msg_t                          */
#include "types.h"

/**
 * @brief   Module descriptor for mod_can_rx (registered in scheduler.c)
 * @brief   mod_can_rx 的模块描述符（在 scheduler.c 中注册）
 */
extern const mod_desc_t mod_can_rx;

/**
 * @brief   Copy the most recent raw 8-byte payload of a CAN frame
 * @brief   复制指定 CAN id 最近收到的 8 字节原始 payload
 *
 * @details The RX tick caches the most recent raw frame per IPK CAN id
          (8-byte payload + dlc, ide/rtr are always 0 in IPK). This API
          exposes that cache so diag / demo modules can read full bytes
          without re-decoding every signal.
 *
 * @param[in]   can_id  IPK RX can_id (11-bit standard)
 * @param[out]  out     Filled on success
 *
 * @return  c02b2_result_t
 * @retval  C02B2_OK            Cache populated
 * @retval  C02B2_ERR_PARAM     can_id not an IPK RX message, or out NULL
 * @retval  C02B2_ERR_NOT_FOUND No frame has been received for this id
 */
c02b2_result_t CanRx_GetLastRawFrame(u32 can_id, can_msg_t *out);

/**
 * @brief   Number of distinct IPK RX messages whose cache holds a frame
 * @brief   已缓存最近一帧的 IPK RX 报文数量
 *
 * @return  u32  Count of cached entries (0 .. CAN_DB_IPK_RX_COUNT)
 */
u32 CanRx_GetRawFrameCount(void);

#endif /* C02B2_MOD_CAN_RX_H */
