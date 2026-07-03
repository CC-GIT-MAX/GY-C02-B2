/**
 * @file    can_db.h
 * @brief   CAN message database
 */
#ifndef LBX_CAN_DB_H
#define LBX_CAN_DB_H

#include "types.h"
#include "can_if.h"

typedef struct {
    u32          can_id;
    u8           ide;
    can_channel_t ch;
    u16          cycle_ms;
    u16          timeout_ms;
    can_rx_cb_t  cb;
} can_rx_desc_t;

typedef struct {
    u32          can_id;
    u8           ide;
    can_channel_t ch;
    u16          cycle_ms;
    void (*pack)(u8 *data);
    u8           dlc;
} can_tx_desc_t;

extern const can_rx_desc_t g_can_rx_db[];
extern const u16          g_can_rx_count;
extern const can_tx_desc_t g_can_tx_db[];
extern const u16          g_can_tx_count;

void CanDb_LogOnInit(void);

#endif /* LBX_CAN_DB_H */
