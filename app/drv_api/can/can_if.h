/**
 * @file    can_if.h
 * @brief   CAN interface abstraction
 *
 * Only this header + can_if.c touch flexcan_driver.h.
 */
#ifndef C02B2_CAN_IF_H
#define C02B2_CAN_IF_H

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
 * @details Reset the RX ring buffer only.  All FlexCAN hardware bring-up
          (FLEXCAN_DRV_Init, FIFO filter configuration, event / error
          callback installation and FIFO priming) is done in Can_Init().
          This function is intentionally lightweight so it can be called
          repeatedly without re-arming the controller.
 *
 * @return  c02b2_result_t
 * @retval  C02B2_OK  Ring buffer reset (always succeeds)
 */
c02b2_result_t CanIf_Init(void);

/**
 * @brief   Install the FlexCAN event + error callbacks for both instances
 * @brief   为两个 FlexCAN 实例安装事件 / 错误回调
 *
 * @details Thin wrapper that registers prv_flexcan_cb (RX/TX events) and
          prv_flexcan_err_cb (bus-off / warning / bit-error) for both
          instances.  Called from Can_Init() so the bring-up sequence
          stays in one place.
 */
void CanIf_InstallFlexcanCallbacks(void);

/**
 * @brief   Prime the RX FIFO for both instances (arm first receive)
 * @brief   启动两个实例的 RX FIFO（首帧接收预热）
 *
 * @details Calls FLEXCAN_DRV_RxFifo(inst, &s_rx_fifo_start_buf) for both
          instances.  This is what enables the FRAME_AVAILABLE /
          WARNING / OVERFLOW interrupts in RX-FIFO mode -- the SDK does
          NOT enable them inside FLEXCAN_DRV_Init, so without this call
          no RX callback will ever fire.
 *
 * @return  c02b2_result_t
 * @retval  C02B2_OK   Both FIFOs primed
 * @retval  C02B2_ERR  At least one FLEXCAN_DRV_RxFifo returned non-SUCCESS
 */
c02b2_result_t CanIf_ArmRxFifo(void);

/**
 * @brief   Send a single CAN frame on the chosen channel
 * @brief   在指定通道上发送一帧 CAN 报文
 *
 * @param[in]  ch   Logical channel
 * @param[in]  msg  Frame to transmit (id/ide/rtr/dlc/data)
 *
 * @return  c02b2_result_t
 * @retval  C02B2_OK        Accepted by the driver
 * @retval  C02B2_ERR_BUSY  Mailbox busy (frame dropped)
 */
c02b2_result_t CanIf_Send(can_channel_t ch, const can_msg_t *msg);

/**
 * @brief   Mailbox layout per channel (after RFFN is applied)
 * @brief   每个通道的邮箱布局（FIFO 配置后）
 *
 * @details Per YTM32B1M Table 18.20:
 *          - public_can  RFFN=8 (72 filters) -> FIFO occupies MB 0..23
 *          - private_can RFFN=6 (56 filters) -> FIFO occupies MB 0..19
 *
 *          The MB windows below are reserved for the caller's use
 *          (single-MB ID-mask RX + TX round-robin).  The constants
 *          are channel-aware because the private bus has 6 extra
 *          MB available for single-MB RX.
 */
typedef struct {
    u8 rx_mb_first;   /**< First single-MB ID-mask RX mailbox (inclusive) */
    u8 rx_mb_last;    /**< Last single-MB ID-mask RX mailbox (inclusive)  */
    u8 tx_mb_first;   /**< First TX round-robin mailbox (always 26)      */
    u8 tx_mb_last;    /**< Last TX round-robin mailbox (always 31)       */
} can_mb_layout_t;

/**
 * @brief   Return the post-FIFO mailbox layout for a channel
 * @brief   返回某通道 FIFO 配置之后的邮箱布局
 *
 * @param[in]  ch  Logical channel
 *
 * @return  can_mb_layout_t  RX/TX mailbox windows for the caller
 */
can_mb_layout_t CanIf_GetMbLayout(can_channel_t ch);

/**
 * @brief   Configure a single RX mailbox with an exact CAN id
 * @brief   把单个接收邮箱配置为精确匹配某个 CAN id
 *
 * @details Uses one of the post-FIFO single-MB slots (24..25 on
 *          public, 20..25 on private) so it does NOT collide with
 *          the FIFO ID filter table or the TX round-robin pool.
 *
 * @param[in]  ch      Logical channel
 * @param[in]  mb_idx  Mailbox index (must be inside the layout's
 *                     rx_mb_first..rx_mb_last window)
 * @param[in]  can_id  11-bit standard id to match exactly
 * @param[in]  ide     0 = STD, 1 = EXT (extended id)
 *
 * @return  c02b2_result_t
 * @retval  C02B2_OK         Mailbox configured and armed for RX
 * @retval  C02B2_ERR_PARAM  mb_idx outside the allowed RX window
 */
c02b2_result_t CanIf_ConfigRxMb(can_channel_t ch, u8 mb_idx,
                                u32 can_id, u8 ide);


/**
 * @brief   Register an RX callback (legacy stub)
 * @brief   注册一个 RX 回调（保留旧 API，无实际操作）
 *
 * @param[in]  ch     Logical channel (unused)
 * @param[in]  can_id CAN id (unused)
 * @param[in]  ide    0=STD, 1=EXT (unused)
 * @param[in]  cb     Callback (unused)
 *
 * @return  c02b2_result_t  Always C02B2_OK
 */
c02b2_result_t CanIf_RegisterRx(can_channel_t ch, u32 can_id, u8 ide, can_rx_cb_t cb);

/**
 * @brief   Globally enable/disable TX on a channel (stub)
 * @brief   全局使能/禁止某通道的发送（占位）
 *
 * @param[in]  ch  Channel
 * @param[in]  en  true = enable, false = disable
 *
 * @return  c02b2_result_t  Always C02B2_OK
 */
c02b2_result_t CanIf_SetTxEnabled(can_channel_t ch, bool en);

/**
 * @brief   Request the channel to enter sleep mode (stub)
 * @brief   请求通道进入休眠模式（占位）
 *
 * @param[in]  ch  Channel
 *
 * @return  c02b2_result_t  Always C02B2_OK
 */
c02b2_result_t CanIf_GoToSleep(can_channel_t ch);

/**
 * @brief   Wake the channel from sleep (stub)
 * @brief   将通道从休眠唤醒（占位）
 *
 * @param[in]  ch  Channel
 *
 * @return  c02b2_result_t  Always C02B2_OK
 */
c02b2_result_t CanIf_WakeUp(can_channel_t ch);

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

#endif /* C02B2_CAN_IF_H */
