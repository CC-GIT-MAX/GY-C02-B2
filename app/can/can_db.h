/**
 * @file    can_db.h
 * @brief   CAN message database
 */
#ifndef LBX_CAN_DB_H
#define LBX_CAN_DB_H

#include "types.h"
#include "can_if.h"

/**
 * @brief   RX message descriptor
 * @brief   接收报文描述符
 */
typedef struct {
    u32          can_id;
    u8           ide;
    can_channel_t ch;
    u16          cycle_ms;
    u16          timeout_ms;
    can_rx_cb_t  cb;
} can_rx_desc_t;

/**
 * @brief   TX message descriptor
 * @brief   发送报文描述符
 */
typedef struct {
    u32          can_id;
    u8           ide;
    can_channel_t ch;
    u16          cycle_ms;
    void (*pack)(u8 *data);
    u8           dlc;
} can_tx_desc_t;

extern const can_rx_desc_t g_can_rx_db[];   /**< RX routing table (AUTOGEN). */
extern const u16          g_can_rx_count;   /**< Entry count of g_can_rx_db[]. */
extern const can_tx_desc_t g_can_tx_db[];   /**< TX packing table (AUTOGEN). */
extern const u16          g_can_tx_count;   /**< Entry count of g_can_tx_db[]. */

/**
 * @brief   Log a one-shot summary of the RX/TX tables
 * @brief   一次性打印 RX/TX 表的概要信息
 *
 * @details Invoked from CanIf_Init() after both controllers are up.
 */
void CanDb_LogOnInit(void);

#endif /* LBX_CAN_DB_H */
