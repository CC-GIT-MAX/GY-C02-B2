/**
 * @file    can_db.h
 * @brief   CAN message database
 *
 * All CAN frames (RX and TX) are described statically. The table
 * is the single source of truth. New messages only require adding
 * one entry to can_db[]; no code edits to dispatch logic.
 */
#ifndef LBX_CAN_DB_H
#define LBX_CAN_DB_H

#include "types.h"
#include "can_if.h"

/* RX frame descriptor */
typedef struct {
    u32          can_id;
    u8           ide;
    can_channel_t ch;
    u16          cycle_ms;     /* expected cycle time           */
    u16          timeout_ms;   /* timeout = 2.5x cycle typical  */
    can_rx_cb_t  cb;           /* NULL until registered         */
} can_rx_desc_t;

/* TX frame descriptor */
typedef struct {
    u32          can_id;
    u8           ide;
    can_channel_t ch;
    u16          cycle_ms;     /* 0 = event-driven              */
    void (*pack)(u8 *data);    /* fills DLC bytes of payload    */
    u8           dlc;
} can_tx_desc_t;

extern const can_rx_desc_t g_can_rx_db[];
extern const u16          g_can_rx_count;
extern const can_tx_desc_t g_can_tx_db[];
extern const u16          g_can_tx_count;

#endif /* LBX_CAN_DB_H */
