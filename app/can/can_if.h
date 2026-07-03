/**
 * @file    can_if.h
 * @brief   CAN interface abstraction
 *
 * Business code includes ONLY this header. Do not include
 * "flexcan_driver.h" from app/ or cluster/. This header is the
 * single point of API surface.
 *
 * Concrete driver (flexcan_driver) is wired in can_if.c and never
 * leaks upward.
 */
#ifndef LBX_CAN_IF_H
#define LBX_CAN_IF_H

#include "types.h"
#include "result.h"

/* Logical CAN channel (independent of physical instance number) */
typedef enum {
    CAN_CH_PRIVATE = 0,    /* chassis / private CAN                */
    CAN_CH_PUBLIC  = 1,    /* body / public CAN                    */
    CAN_CH_MAX
} can_channel_t;

/* Standard CAN ID 11-bit or extended 29-bit (use the IDE bit in flags) */
typedef struct {
    u32 id;            /* CAN identifier (11 or 29 bit)       */
    u8  ide;           /* 0 = std, 1 = ext                    */
    u8  rtr;           /* 0 = data, 1 = remote                */
    u8  dlc;           /* 0..8                                */
    u8  data[8];
} can_msg_t;

/* RX callback registered per message */
typedef void (*can_rx_cb_t)(const can_msg_t *msg);

/* TX API: scheduled transmit (called from tick) */
lbx_result_t CanIf_Init(void);
lbx_result_t CanIf_RegisterRx(can_channel_t ch, u32 can_id, u8 ide, can_rx_cb_t cb);
lbx_result_t CanIf_Send(can_channel_t ch, const can_msg_t *msg);
lbx_result_t CanIf_SetTxEnabled(can_channel_t ch, bool en);
lbx_result_t CanIf_GoToSleep(can_channel_t ch);
lbx_result_t CanIf_WakeUp(can_channel_t ch);

/* Status queries for higher layers (e.g. bus-off recovery) */
bool    CanIf_IsBusOff(can_channel_t ch);
u32     CanIf_GetBusOffCount(can_channel_t ch);

#endif /* LBX_CAN_IF_H */
