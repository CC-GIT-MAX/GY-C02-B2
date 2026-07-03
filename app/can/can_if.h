/**
 * @file    can_if.h
 * @brief   CAN interface abstraction
 *
 * Only this header + can_if.c touch flexcan_driver.h.
 */
#ifndef LBX_CAN_IF_H
#define LBX_CAN_IF_H

#include "types.h"
#include "result.h"

typedef enum {
    CAN_CH_PRIVATE = 0,
    CAN_CH_PUBLIC  = 1,
    CAN_CH_MAX
} can_channel_t;

typedef struct {
    u32 id;
    u8  ide;
    u8  rtr;
    u8  dlc;
    u8  data[8];
} can_msg_t;

typedef void (*can_rx_cb_t)(const can_msg_t *msg);

lbx_result_t CanIf_Init(void);
lbx_result_t CanIf_Send(can_channel_t ch, const can_msg_t *msg);
lbx_result_t CanIf_SetTxEnabled(can_channel_t ch, bool en);
lbx_result_t CanIf_GoToSleep(can_channel_t ch);
lbx_result_t CanIf_WakeUp(can_channel_t ch);
bool    CanIf_IsBusOff(can_channel_t ch);
u32     CanIf_GetBusOffCount(can_channel_t ch);

/* Bridge to can_rx/can_tx modules */
bool CanIf_PopRx(can_msg_t *out);
u32  CanIf_RxPending(void);

#endif /* LBX_CAN_IF_H */
