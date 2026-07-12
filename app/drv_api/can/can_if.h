/**
 * @file    can_if.h
 * @brief   CAN interface abstraction
 *
 * Only this header + can_if.c touch flexcan_driver.h.
 *
 * Layering (top -> bottom):
 *   app/mod_can_demo        <- may use either app/can/can_tx.h + can_rx.h
 *                              OR the CanIf_Tx<...> / CanIf_Rx<...> wrappers in this
 *                              header
 *   app/can/can_tx.c        <- CanTx_PreparePayload / EncodeSignal / Trigger
 *   app/can/can_rx.c        <- CanRx_GetLastRawFrame / GetRawFrameCount
 *   app/drv_api/can/can_if  <- CanIf_Send / PopRx / ConfigRxMb  <-- THIS FILE
 *                              + DBC-aware wrappers (CanIf_TxPreparePayload,
 *                                CanIf_TxEncodeSignal, CanIf_TxTrigger,
 *                                CanIf_RxGetLastRawFrame, ...)
 *   SDK flexcan_driver      <- FLEXCAN_DRV_*
 *
 * DBC-aware entry points (send whole payload / send one signal /
 * receive last frame by CAN id) are available two ways:
 *   1. Directly from app/can/can_tx.h + can_rx.h (the implementation).
 *   2. As CanIf_Tx<...> / CanIf_Rx<...> wrappers in this header (forwarded by
 *      can_if.c).  These exist so demo / diag modules only need to
 *      include can_if.h.
 *
 * The wrappers add an app/drv_api/can/ -> app/can/ dependency, but
 * only one-way (can_tx / can_rx never reach back into can_if
 * internals).
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
 * @details 仅复位 RX 环形缓冲区。所有 FlexCAN 硬件初始化
 *          （FLEXCAN_DRV_Init、FIFO 过滤配置、事件 / 错误
 *          回调安装及 FIFO 预启动）均由 Can_Init() 完成。
 *          本函数刻意保持轻量，可重复调用而无需重新
 *          启动控制器。
 *
 * @return  c02b2_result_t
 * @retval  C02B2_OK  Ring buffer reset (always succeeds)
 */
c02b2_result_t CanIf_Init(void);

/**
 * @brief   Install the FlexCAN event + error callbacks for both instances
 * @brief   为两个 FlexCAN 实例安装事件 / 错误回调
 *
 * @details 薄封装，为两个实例注册 prv_flexcan_cb（RX/TX 事件）
 *          与 prv_flexcan_err_cb（bus-off / warning / bit-error）。
 *          由 Can_Init() 在内部调用一次（不要从应用层直接调用），
 *          使初始化序列集中在一处。重复安装回调会与 prv_flexcan_cb
 *          内部 s_if_inited 状态冲突，应避免。
 */
void CanIf_InstallFlexcanCallbacks(void);

/**
 * @brief   Initialize both FlexCAN instances (driver-level bring-up)
 * @brief   初始化两个 FlexCAN 实例（驱动级初始化）
 *
 * @details 单一入口，依次执行完整的 FlexCAN 初始化序列
 *          （Init -> ConfigRxFifo -> InstallCallbacks ->
 *          ArmRxFifo）。由 app/init/drv_init.c 调用一次。
 *          由于与 can_if 其余部分操作同一硬件状态，
 *          本声明放在此头文件；原 app/drv_api/can/can_init.c
 *          已合并入 can_if.c。
 */
void Can_Init(void);

/**
 * @brief   Prime the RX FIFO for both instances (arm first receive)
 * @brief   启动两个实例的 RX FIFO（首帧接收预热）
 *
 * @details 对两个实例分别调用 FLEXCAN_DRV_RxFifo(inst,
 *          &s_rx_fifo_start_buf)。该调用会开启 RX-FIFO 模式
 *          下的 FRAME_AVAILABLE / WARNING / OVERFLOW 中断，
 *          SDK 不会在 FLEXCAN_DRV_Init 内部启用，
 *          若不调用此函数则永远不会有 RX 回调触发。
 *
 * @return  c02b2_result_t
 * @retval  C02B2_OK   Both FIFOs primed
 * @retval  C02B2_ERR  At least one FLEXCAN_DRV_RxFifo returned non-SUCCESS
 */
c02b2_result_t CanIf_ArmRxFifo(void);

/**
 * @brief   Drain pending soft-recovery requests (5 ms tick context)
 * @brief   处理挂起的软恢复请求(5 ms tick 上下文)
 *
 * @details prv_flexcan_err_cb() 在 ISR 中置位各通道的 pending 标志；
 *          本函数在调度器 5 ms tick 中实际执行
 *          deinit + init + filter-refill + re-arm-FIFO 流程。
 *          禁止在 ISR 中调用（会访问外设 RAM / NVIC）。
 *
 * @note    Safe to call when nothing is pending - the per-channel
 *          flag is the gate.  Always returns C02B2_OK today; the
 *          status is reserved for future "recovery failed" cases.
 */
c02b2_result_t CanIf_RecoverPump(void);

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
 * @details 依据 YTM32B1M 表 18.20：
 *          - public_can  RFFN=8（72 个过滤器）-> FIFO 占用 MB 0..23
 *          - private_can RFFN=6（56 个过滤器）-> FIFO 占用 MB 0..19
 *
 *          下方 MB 窗口保留给调用方使用
 *          （单 MB ID-mask RX + TX 轮询）。
 *          各通道下常量不同，因为 private 总线多出 6 个 MB
 *          可用于单 MB RX。
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
 * @details 使用 FIFO 之后的单 MB 槽位
 *          （public 上 24..25，private 上 20..25），
 *          避免与 FIFO ID 过滤表及 TX 轮询池冲突。
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

/* ---------------------------------------------------------------- *
 *  DBC-aware convenience APIs (used by mod_can_demo / diag)
 *
 *  These thin wrappers forward to app/can/can_tx.c + can_rx.c so the
 *  caller only needs to include can_if.h.  They address CAN frames by
 *  the 11-bit IPK CAN id (NOT by mailbox index or channel), which is
 *  what demo / diag modules already work with.
 *
 *  Layering note: can_if lives in app/drv_api/can/ but these four
 *  entry points call into app/can/can_tx.h + can_rx.h.  The dependency
 *  is intentional and one-way -- can_tx / can_rx never reach into
 *  can_if internals.
 * ---------------------------------------------------------------- */

/**
 * @brief   Fill a TX message payload (whole-buffer)
 * @brief   填充一条 TX 报文 payload（整报文）
 *
 * @details 转发至 CanTx_PreparePayload()。完整语义
 *          见 app/can/can_tx.h。
 *
 * @param[in]  can_id  IPK TX message can_id (11-bit standard)
 * @param[in]  data    Source buffer (at least `dlc` bytes)
 * @param[in]  dlc     Data length (0..8)
 *
 * @return  c02b2_result_t  See CanTx_PreparePayload()
 */
c02b2_result_t CanIf_TxPreparePayload(u32 can_id, const u8 *data, u8 dlc);

/**
 * @brief   Update a single signal inside an existing TX payload
 * @brief   在已有 TX payload 中更新单个信号
 *
 * @details 转发至 CanTx_EncodeSignal()。同报文中的
 *          其他信号保持原值。
 *
 * @param[in]  can_id  IPK TX message can_id
 * @param[in]  sig_id  CAN_DB_SIG_* signal id belonging to that message
 * @param[in]  raw     Raw bit pattern (NOT physical -- the caller must
 *                     already have applied factor/offset if needed)
 *
 * @return  c02b2_result_t  See CanTx_EncodeSignal()
 */
c02b2_result_t CanIf_TxEncodeSignal(u32 can_id, u16 sig_id, u32 raw);

/**
 * @brief   Force immediate send of a single TX frame (event-driven)
 * @brief   强制立即发送一帧（事件驱动）
 *
 * @details 转发至 CanTx_Trigger()。调用前需已通过
 *          CanIf_TxPreparePayload() 或 CanIf_TxEncodeSignal()
 *          完成 payload 准备。
 *
 * @param[in]  can_id  11-bit CAN identifier (must match a TX db entry)
 *
 * @return  c02b2_result_t  See CanTx_Trigger()
 */
c02b2_result_t CanIf_TxTrigger(u32 can_id);

/**
 * @brief   Copy the most recent raw 8-byte payload of a CAN frame
 * @brief   复制指定 CAN id 最近收到的 8 字节原始 payload
 *
 * @details 转发至 CanRx_GetLastRawFrame()。RX tick 按
 *          IPK CAN id 缓存一帧（8 字节 payload + dlc）。
 *
 * @param[in]   can_id  IPK RX can_id (11-bit standard)
 * @param[out]  out     Filled on success
 *
 * @return  c02b2_result_t  See CanRx_GetLastRawFrame()
 */
c02b2_result_t CanIf_RxGetLastRawFrame(u32 can_id, can_msg_t *out);

/**
 * @brief   Number of distinct IPK RX messages whose cache holds a frame
 * @brief   已缓存最近一帧的 IPK RX 报文数量
 *
 * @details 转发至 CanRx_GetRawFrameCount()。
 *
 * @return  u32  Count of cached entries (0 .. CAN_DB_IPK_RX_COUNT)
 */
u32 CanIf_RxGetRawFrameCount(void);

#endif /* C02B2_CAN_IF_H */
