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
 * @details RX tick 按 IPK CAN id 缓存最近收到的原始帧
          (8 字节 payload + dlc，IPK 中 ide/rtr 始终为 0)。
          此 API 把缓存暴露出来，使 diag / demo 模块能在不重解
          每个信号的前提下读取完整字节。 *
 * @param[in]   can_id  IPK RX can_id (11-bit standard)
 * @param[out]  out     Filled on success
 *
 * @return  c02b2_result_t    C02B2_OK: Cache populated  C02B2_ERR_PARAM: can_id not an IPK RX message, or out NULL  C02B2_ERR_NOT_FOUND: No frame has been received for this id
 */
c02b2_result_t CanRx_GetLastRawFrame(u32 can_id, can_msg_t *out);

/**
 * @brief   Number of distinct IPK RX messages whose cache holds a frame
 * @brief   已缓存最近一帧的 IPK RX 报文数量
 *
 * @return  u32  Count of cached entries (0 .. CAN_DB_IPK_RX_COUNT)
 */
u32 CanRx_GetRawFrameCount(void);



/* can_rx_freshness_t is owned by can_if.h (single definition); re-exported via the existing #include "drv_api/can/can_if.h" above. */


/**
 * @brief   Check whether a given IPK CAN id is currently in timeout
 * @brief   查询某 IPK CAN id 当前是否超时
 *
 * @details 质量位只在超时被置位；保留超时前的接收值。
 *          未知 / 未被监控的 can_id 返回 false。
 *
 * @param[in]  can_id  IPK 标准 11-bit can_id
 *
 * @return  bool    true: 当前处于超时状态  false: 未超时、从未收到、或 can_id 不在 IPK 表中
 */
bool CanRx_IsMsgTimedOut(u32 can_id);

/**
 * @brief   Resolve the freshness of a given IPK CAN id
 * @brief   查询某 IPK CAN id 的接收状态枚举
 *
 * @param[in]   can_id  IPK 标准 11-bit can_id
 * @param[out]  out     填充状态枚举值
 *
 * @return  c02b2_result_t    C02B2_OK: 成功（*out 已填充）
            C02B2_ERR_PARAM: out 为 NULL 或 can_id 不在 IPK 表中
 */
c02b2_result_t CanRx_GetMsgFreshness(u32 can_id, can_rx_freshness_t *out);

#endif /* C02B2_MOD_CAN_RX_H */
