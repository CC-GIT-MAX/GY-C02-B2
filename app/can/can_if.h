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

/**
 * @brief   Logical CAN channel identifiers
 * @brief   逻辑 CAN 通道编号
 */
typedef enum {
    CAN_CH_PRIVATE = 0,
    CAN_CH_PUBLIC  = 1,
    CAN_CH_MAX
} can_channel_t;

/**
 * @brief   Plain CAN frame descriptor (channel-agnostic)
 * @brief   与通道无关的 CAN 报文描述符
 */
typedef struct {
    u32 id;
    u8  ide;
    u8  rtr;
    u8  dlc;
    u8  data[8];
} can_msg_t;

/** @brief  Callback signature for received frames (tick context). */
typedef void (*can_rx_cb_t)(const can_msg_t *msg);

/**
 * @brief   Initialize both CAN channels and the RX ring buffer
 * @brief   初始化两条 CAN 通道及接收环形缓冲
 *
 * @return  lbx_result_t
 * @retval  LBX_OK  Both channels up
 * @retval  LBX_ERR At least one FLEXCAN_DRV_Init failed
 */
lbx_result_t CanIf_Init(void);

/**
 * @brief   Send a single CAN frame on the chosen channel
 * @brief   在指定通道上发送一帧 CAN 报文
 *
 * @param[in]  ch   Logical channel
 * @param[in]  msg  Frame to transmit (id/ide/rtr/dlc/data)
 *
 * @return  lbx_result_t
 * @retval  LBX_OK        Accepted by the driver
 * @retval  LBX_ERR_BUSY  Mailbox busy (frame dropped)
 */
lbx_result_t CanIf_Send(can_channel_t ch, const can_msg_t *msg);

/**
 * @brief   Register an RX callback (legacy stub)
 * @brief   注册一个 RX 回调（保留旧 API，无实际操作）
 *
 * @param[in]  ch     Logical channel (unused)
 * @param[in]  can_id CAN id (unused)
 * @param[in]  ide    0=STD, 1=EXT (unused)
 * @param[in]  cb     Callback (unused)
 *
 * @return  lbx_result_t  Always LBX_OK
 */
lbx_result_t CanIf_RegisterRx(can_channel_t ch, u32 can_id, u8 ide, can_rx_cb_t cb);

/**
 * @brief   Globally enable/disable TX on a channel (stub)
 * @brief   全局使能/禁止某通道的发送（占位）
 *
 * @param[in]  ch  Channel
 * @param[in]  en  true = enable, false = disable
 *
 * @return  lbx_result_t  Always LBX_OK
 */
lbx_result_t CanIf_SetTxEnabled(can_channel_t ch, bool en);

/**
 * @brief   Request the channel to enter sleep mode (stub)
 * @brief   请求通道进入休眠模式（占位）
 *
 * @param[in]  ch  Channel
 *
 * @return  lbx_result_t  Always LBX_OK
 */
lbx_result_t CanIf_GoToSleep(can_channel_t ch);

/**
 * @brief   Wake the channel from sleep (stub)
 * @brief   将通道从休眠唤醒（占位）
 *
 * @param[in]  ch  Channel
 *
 * @return  lbx_result_t  Always LBX_OK
 */
lbx_result_t CanIf_WakeUp(can_channel_t ch);

/**
 * @brief   Test whether the channel is in bus-off state (stub)
 * @brief   测试通道是否处于 bus-off 状态（占位）
 *
 * @param[in]  ch  Channel
 *
 * @return  bool
 * @retval  true   Bus-off recorded (stub: always false)
 * @retval  false  Not in bus-off
 */
bool CanIf_IsBusOff(can_channel_t ch);

/**
 * @brief   Get the cumulative bus-off recovery count (stub)
 * @brief   获取累计 bus-off 恢复次数（占位）
 *
 * @param[in]  ch  Channel
 *
 * @return  u32  Bus-off count (stub: always 0)
 */
u32 CanIf_GetBusOffCount(can_channel_t ch);

/**
 * @brief   Pop one received frame from the ring (called by can_rx tick)
 * @brief   从环形缓冲弹出一帧接收报文（can_rx tick 中调用）
 *
 * @param[out] out  Populated on success
 *
 * @return  bool
 * @retval  true   Frame popped
 * @retval  false  Ring empty
 */
bool CanIf_PopRx(can_msg_t *out);

/**
 * @brief   Number of frames currently waiting in the RX ring
 * @brief   接收环中当前等待的报文数量
 *
 * @return  u32  Count of pending frames
 */
u32 CanIf_RxPending(void);

#endif /* LBX_CAN_IF_H */
