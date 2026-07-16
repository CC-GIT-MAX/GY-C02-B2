/**
 * @file    can_if.h
 * @brief   CAN interface abstraction (single entry point for business layer)
 * @brief   CAN 接口适配层 (业务层统一入口)
 *
 * Only this header + can_if.c touch flexcan_driver.h.
 *
 * Layering (top -> bottom):
 *   app/mod_<...>            <- business modules: only include this file
 *   app/can/can_tx.c         <- CanTx_* implementation (forwarded by can_if)
 *   app/can/can_rx.c         <- CanRx_* implementation (forwarded by can_if)
 *   app/drv_api/can/can_db.c <- DBC dispatch + bit-level codec
 *   app/drv_api/can/can_if   <- THIS FILE: sole flexcan_driver touchpoint
 *   SDK flexcan_driver       <- FLEXCAN_DRV_*
 *
 * Business-layer rule: any CAN-related call must go through CanIf_*
 * (or the plain Can_* / CanIf_Init / Can_Init lifecycle entry points).
 * Direct inclusion of app/can/can_tx.h, can_rx.h, can_db.h, can_db_codec.h,
 * can_db_ipk_gen.h is NOT allowed outside this directory.
 *
 * Sections (in this header, top -> bottom):
 *   1. Data types / channel id
 *   2. Lifecycle / hardware bring-up
 *   3. Channel control / state (send / sleep / bus-off)
 *   4. RX ring (channel-agnostic raw frames)
 *   5. TX (DBC-aware, addressed by 11-bit CAN id)
 *   6. RX (DBC-aware, addressed by 11-bit CAN id)
 *   7. DBC metadata query (find message / signal / bus-id mapping)
 *   8. Bit-level codec primitives (PackSignal / GetRaw / DecodeSignal / ...)
 *   9. Mailbox layout (post-FIFO windows) + bus-off recovery
 */
#ifndef C02B2_CAN_IF_H
#define C02B2_CAN_IF_H

#include "types.h"
#include "result.h"
#include "can_db_codec.h"   /* can_sig_desc_t / can_raw_t for codec primitives */
#include "can_db.h"         /* sig_timeout_policy_t -- re-exported so business */
                            /*   modules need only include can_if.h */


/* ================================================================== *
 *  Section 1: Data types / channel id
 * ================================================================== */

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


/**
 * @brief   Per-CAN-id RX freshness state
 * @brief   按 CAN id 划分的接收状态
 *
 * @details Mirrored from can_rx.h so can_if.h can stand alone (can_rx.h
 *          itself includes can_if.h -- a direct re-export would create
 *          a header cycle). Keep the two definitions in sync.
 */
typedef enum {
    CAN_RX_FRESH_NEVER     = 0,  /**< 启动后未收到过该 can_id */
    CAN_RX_FRESH_OK        = 1,  /**< 当前在 timeout 窗口内（最近一帧未超时） */
    CAN_RX_FRESH_TIMED_OUT = 2,  /**< 一旦收到过，但当前已超时（值保留未动） */
} can_rx_freshness_t;

/** @brief  Callback signature for received frames (tick context). */
typedef void (*can_rx_cb_t)(const can_msg_t *msg);

/* ================================================================== *
 *  Section 2: Lifecycle / hardware bring-up
 *
 *  Call CanIf_Init() after BSP / DRV; call Can_Init() once at
 *  drv_init to bring up FlexCAN instances.
 * ================================================================== */

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
 * @note    可从 BSP / DRV / RTI / Scheduler_Init 任何阶段调用
 *          （包括 main.c 主循环之前）；多次调用仅重置 RX ring。
 *          s_if_inited 标记保证幂等。不依赖 FlexCAN 硬件启动顺序
 *          —— Can_Init() 之后调或之前调都不破坏状态。
 *
 * @return  c02b2_result_t    C02B2_OK: Ring buffer reset (always succeeds)
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
 */
void Can_Init(void);

/**
 * @brief   Prime the RX FIFO for both instances (arm first receive)
 * @brief   启动两个实例的 RX FIFO（首帧接收预热）
 *
 * @details 对两个实例分别调用 FLEXCAN_DRV_RxFifo(inst,
 *          &s_rx_fifo_start_buf)。该调用会开启 RX-FIFO 模式
 *          下的 FRAME_AVAILABLE / WARNING / OVERFLOW 中断。
 *
 * @return  c02b2_result_t    C02B2_OK: Both FIFOs primed  C02B2_ERR: At least one
 *                             FLEXCAN_DRV_RxFifo returned non-SUCCESS
 */
c02b2_result_t CanIf_ArmRxFifo(void);

/**
 * @brief   Drain pending soft-recovery requests (5 ms tick context)
 * @brief   处理挂起的软恢复请求(5 ms tick 上下文)
 *
 * @details prv_flexcan_err_cb() 在 ISR 中置位 per-channel pending
 *          标记；本函数在 5 ms tick 中消费标记并执行恢复动作。
 *
 * @return  c02b2_result_t    C02B2_OK: Always (recover action idempotent)
 */
c02b2_result_t CanIf_ProcessPendingRecovers(void);

/* ================================================================== *
 *  Section 3: Channel control / state
 * ================================================================== */

/**
 * @brief   Send a single CAN frame on the chosen channel
 * @brief   在指定通道上发送一帧 CAN 报文
 *
 * @param[in]  ch   Logical channel
 * @param[in]  msg  Frame to transmit (id/ide/rtr/dlc/data)
 *
 * @return  c02b2_result_t    C02B2_OK: Accepted by the driver  C02B2_ERR_BUSY: Mailbox busy (frame dropped)
 */
c02b2_result_t CanIf_Send(can_channel_t ch, const can_msg_t *msg);

/**
 * @brief   Enable or disable TX on the channel (stub)
 * @brief   启用或禁用通道 TX（占位）
 *
 * @param[in]  ch   Channel
 * @param[in]  en   true = enable, false = disable
 *
 * @return  c02b2_result_t    C02B2_OK: Always
 */
c02b2_result_t CanIf_SetTxEnabled(can_channel_t ch, bool en);

/**
 * @brief   Request the channel to enter sleep mode (stub)
 * @brief   请求通道进入休眠模式（占位）
 *
 * @param[in]  ch  Channel
 *
 * @return  c02b2_result_t    C02B2_OK: Always
 */
c02b2_result_t CanIf_GoToSleep(can_channel_t ch);

/**
 * @brief   Wake the channel from sleep (stub)
 * @brief   将通道从休眠唤醒（占位）
 *
 * @param[in]  ch  Channel
 *
 * @return  c02b2_result_t    C02B2_OK: Always
 */
c02b2_result_t CanIf_WakeUp(can_channel_t ch);

/**
 * @brief   Test whether the channel is in bus-off state (stub)
 * @brief   测试通道是否处于 bus-off 状态（占位）
 *
 * @param[in]  ch  Channel
 *
 * @return  bool    true: Bus-off recorded (stub: always false)  false: Not in bus-off
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

/* ================================================================== *
 *  Section 4: RX ring (channel-agnostic raw frames)
 *
 *  Low-level path used by app/can/can_rx.c tick.  Business modules
 *  normally consume DBC-decoded signals via Section 6 instead.
 * ================================================================== */

/**
 * @brief   Pop one received frame from the ring (called by can_rx tick)
 * @brief   从环形缓冲弹出一帧接收报文（can_rx tick 中调用）
 *
 * @param[out] out  Populated on success
 *
 * @return  bool    true: Frame popped  false: Ring empty
 */
bool CanIf_PopRx(can_msg_t *out);

/**
 * @brief   Number of frames currently waiting in the RX ring
 * @brief   接收环中当前等待的报文数量
 *
 * @return  u32  Count of pending frames
 */
u32 CanIf_RxPending(void);

/* ================================================================== *
 *  Section 5: TX (DBC-aware, addressed by 11-bit CAN id)
 *
 *  Forwards to app/can/can_tx.c.  Business modules address TX
 *  frames by IPK can_id, not by mailbox index.
 * ================================================================== */

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
 * @return  c02b2_result_t    C02B2_OK: Payload queued  C02B2_ERR_PARAM: can_id not an IPK TX message, or data NULL
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
 * @return  c02b2_result_t    C02B2_OK: Signal packed  C02B2_ERR_PARAM: can_id not TX, or sig_id not in it
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
 * @return  c02b2_result_t    C02B2_OK: Marked as pending  C02B2_ERR_PARAM: No matching TX entry
 */
c02b2_result_t CanIf_TxTrigger(u32 can_id);

/**
 * @brief   Override the cyclic period for a TX message
 * @brief   覆盖某条 TX 报文的发送周期
 *
 * @details 转发至 CanTx_SetCycle()。0 = event-driven only。
 *
 * @param[in]  can_id     IPK TX message can_id
 * @param[in]  cycle_ms   Period in ms (0 = event-driven only)
 *
 * @return  c02b2_result_t    C02B2_OK: Period set  C02B2_ERR_PARAM: can_id not an IPK TX message
 */
c02b2_result_t CanIf_TxSetCycle(u32 can_id, u16 cycle_ms);

/**
 * @brief   Rebuild a TX payload from every signal via Signal_Get()
 * @brief   从 signal bus 重新读所有信号, 全量重建一条 TX 报文的 payload
 *
 * @details 转发至 CanTx_RebuildFromSignals()。注意副作用:
 *          详见 app/can/can_tx.h::CanTx_RebuildFromSignals 的 @warning。
 *
 * @param[in]  can_id  IPK TX message can_id
 *
 * @return  c02b2_result_t    C02B2_OK: Payload rebuilt  C02B2_ERR_PARAM: can_id not a TX message
 */
c02b2_result_t CanIf_TxRebuildFromSignals(u32 can_id);

/* ================================================================== *
 *  Section 6: RX (DBC-aware, addressed by 11-bit CAN id)
 *
 *  Forwards to app/can/can_rx.c.  Business modules read cached raw
 *  frames and freshness by IPK can_id.
 * ================================================================== */

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
 * @return  c02b2_result_t    C02B2_OK: Cache populated  C02B2_ERR_PARAM: not an IPK RX msg, or out NULL
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

/**
 * @brief   Check whether a given IPK CAN id is currently in timeout
 * @brief   查询某 IPK CAN id 当前是否超时
 *
 * @details 转发至 CanRx_IsMsgTimedOut()。质量位只在超时被置位；
 *          保留超时前的接收值。未知 / 未被监控的 can_id 返回 false。
 *
 * @param[in]  can_id  IPK standard 11-bit can_id
 *
 * @return  bool    true: Currently in timeout  false: Not timed out / never received / not in IPK table
 */
bool CanIf_RxIsMsgTimedOut(u32 can_id);

/**
 * @brief   Resolve the freshness of a given IPK CAN id
 * @brief   查询某 IPK CAN id 的接收状态枚举
 *
 * @details 转发至 CanRx_GetMsgFreshness()。
 *
 * @param[in]   can_id  IPK standard 11-bit can_id
 * @param[out]  out     Populated with the freshness enum value
 *
 * @return  c02b2_result_t    C02B2_OK: *out filled  C02B2_ERR_PARAM: out NULL or can_id not in IPK table
 */
c02b2_result_t CanIf_RxGetMsgFreshness(u32 can_id, can_rx_freshness_t *out);

/* ================================================================== *
 *  Section 7: DBC metadata query
 *
 *  Forwards to app/drv_api/can/can_db.c.  Business modules use these
 *  to look up message / signal descriptors by CAN id or enum id,
 *  translate between DBC signal ids and signal-bus ids, and configure
 *  per-signal timeout policy.
 * ================================================================== */

/**
 * @brief   Find an IPK message descriptor by can_id
 * @brief   按 can_id 查找 IPK 报文描述符
 *
 * @details 转发至 CanDb_FindIpkById()。
 *
 * @param[in]  can_id  Standard 11-bit can_id
 *
 * @return  const can_msg_desc_t*  Pointer into can_msg_descs_ipk[], or NULL
 */
const can_msg_desc_t *CanIf_FindMsgById(u32 can_id);

/**
 * @brief   Find an IPK signal descriptor by enum id
 * @brief   按枚举 id 查找 IPK 信号描述符
 *
 * @details 转发至 CanDb_FindIpkSig()。
 *
 * @param[in]  sig_id  CAN_DB_SIG_* enum value
 *
 * @return  const can_sig_desc_t*  Pointer into can_sig_descs_ipk[], or NULL
 */
const can_sig_desc_t *CanIf_FindSig(u16 sig_id);

/**
 * @brief   Translate a DBC signal enum id to the corresponding
 *          signal-bus id (SIG_CAN_<Name> in app/signal/signal.h)
 * @brief   把 DBC 信号枚举 id 转换为 signal bus 上的对应 id
 *
 * @details 转发至 CanDb_DbcSigToBus()。
 *
 * @param[in]  db_sig_id  CAN_DB_SIG_* enum value (>0)
 *
 * @return  signal_id_t  Matching SIG_CAN_* id, or SIG_INVALID if out of range
 */
signal_id_t CanIf_DbcSigToBus(u16 db_sig_id);

/**
 * @brief   Override the per-signal timeout policy for one bus id
 * @brief   设置单个 bus 上信号的超时策略
 *
 * @details 转发至 CanDb_SetSignalTimeoutPolicy()。下次 OK→TIMED_OUT
 *          边沿检测时即生效。
 *
 * @param[in]  bus_id   Target signal bus id
 * @param[in]  policy   SIG_TIMEOUT_INIT_DBC 或 SIG_TIMEOUT_KEEP_LAST
 *
 * @return  c02b2_result_t    C02B2_OK: Policy set  C02B2_ERR_PARAM: bus_id 越界 (SIG_INVALID / out of range)
 */
c02b2_result_t CanIf_SetSignalTimeoutPolicy(signal_id_t bus_id, sig_timeout_policy_t policy);

/* ================================================================== *
 *  Section 8: Bit-level codec primitives
 *
 *  Forwards to app/drv_api/can/can_db_codec.c.  Used by business
 *  modules that need to pack / unpack / decode individual signals
 *  (e.g. demo, diag).  Bit-level helpers (BitExtract / BitEncode /
 *  BitExtractSigned) are intentionally NOT exposed here -- business
 *  modules should always go through the signal-descriptor-aware
 *  helpers below.
 * ================================================================== */

/**
 * @brief   Pack a signal's raw value into a CAN payload buffer
 *          at the bit position defined by `sig`
 * @brief   将信号的原始值按 sig 定义的位位置写入 CAN payload
 *
 * @details 转发至 CanDb_PackSignal()。位宽超出 sig->length 时截断。
 *
 * @param[out] data  8-byte payload buffer
 * @param[in]  sig   Signal descriptor (start/length/order)
 * @param[in]  raw   Raw value (truncated to `sig->length` bits)
 */
void CanIf_PackSignal(u8 *data, const can_sig_desc_t *sig, can_raw_t raw);

/**
 * @brief   Extract a signal as a RAW (un-decoded) value from a payload
 * @brief   从 payload 中按信号描述符抽取信号的 RAW(未解码)值
 *
 * @details 转发至 CanDb_GetRaw()。raw-on-the-bus 策略: 不应用 factor/offset,
 *          不走量化。
 *
 * @param[in]  data  8-byte payload (Intel or Motorola)
 * @param[in]  sig   Signal descriptor (start/length/order/signed)
 *
 * @return  u32    raw bit pattern
 */
u32 CanIf_GetRaw(const u8 *data, const can_sig_desc_t *sig);

/**
 * @brief   Decode a signal descriptor's field from a payload into
 *          the quantised physical value
 * @brief   从 payload 中按信号描述符解析字段, 转换成 int32 信号总线表示
 *
 * @details 转发至 CanDb_DecodeSignal()。physical = raw * factor + offset,
 *          四舍五入到 int32。
 *
 * @param[in]  data  8-byte payload (Intel or Motorola)
 * @param[in]  sig   Signal descriptor (start/length/order/signed/factor/offset)
 *
 * @return  s32  Quantised physical value (raw * factor + offset)
 */
s32 CanIf_DecodeSignal(const u8 *data, const can_sig_desc_t *sig);

/**
 * @brief   Decode a physical s32 into a u32 raw for the payload
 * @brief   把 int32 信号总线值转换为 CAN payload 应承载的原始值
 *
 * @details 转发至 CanDb_EncodeSignalValue()。raw = round((value - offset) / factor),
 *          饱和到 sig->length 位。
 *
 * @param[in]  value  Physical value from Signal_Get
 * @param[in]  sig    Signal descriptor (factor/offset/length/signed)
 *
 * @return  can_raw_t  Raw value (will fit in `sig->length` bits)
 */
can_raw_t CanIf_EncodeSignalValue(s32 value, const can_sig_desc_t *sig);

/**
 * @brief   Convenience: encode a physical s32 AND pack it into the payload
 * @brief   便捷函数: 一步完成 s32 物理量 + payload 写入
 *
 * @details 转发至 CanDb_EncodeAndPack()。仅在调用方手上有 s32 物理量
 *          (例如公式推导)时使用; raw-loopback 路径请用
 *          CanIf_PackSignal() 避免 (value - offset) / factor 往返。
 *
 * @param[out] data   8-byte payload buffer
 * @param[in]  sig    Signal descriptor
 * @param[in]  value  Physical value (bus-level, s32)
 */
void CanIf_EncodeAndPack(u8 *data, const can_sig_desc_t *sig, s32 value);


/* ================================================================== *
 *  Section 9: Mailbox layout (post-FIFO windows) + bus-off recovery
 * ================================================================== */

/**
 * @brief   Post-FIFO mailbox index windows for one CAN channel
 * @brief   单个 CAN 通道 FIFO 之后的邮箱索引窗口
 *
 * @details Reflects the hard-coded layout from board/can_config.c:
 *          - public_can  : RX 24..25, TX CAN_TX_MB_FIRST..CAN_TX_MB_LAST
 *          - private_can : RX 20..25, TX CAN_TX_MB_FIRST..CAN_TX_MB_LAST
 *
 *          Used by callers that need to allocate exact-match RX
 *          mailboxes or poll TX slots without hard-coding numbers.
 */
typedef struct {
    u8 rx_mb_first;
    u8 rx_mb_last;
    u8 tx_mb_first;
    u8 tx_mb_last;
} can_mb_layout_t;

/**
 * @brief   Return the post-FIFO mailbox layout for a channel
 * @brief   返回某通道 FIFO 之后的邮箱布局
 *
 * @param[in]  ch  Logical channel
 *
 * @return  can_mb_layout_t  RX/TX mailbox windows for the caller
 */
can_mb_layout_t CanIf_GetMbLayout(can_channel_t ch);

/**
 * @brief   Recover the TX pump after bus-off / error passive
 * @brief   总线关闭 / 错误被动后恢复发送泵
 *
 * @details Walks both channels (CAN_CH_PRIVATE, CAN_CH_PUBLIC); for any
 *          channel whose s_recovery_pending flag was raised by the FlexCAN
 *          error ISR, performs FLEXCAN_DRV_Deinit + Init + RX-FIFO
 *          re-prime outside ISR context. Implemented in can_if.c;
 *          called from can_rx.c::prv_tick() at 5 ms cadence.
 *
 * @return  c02b2_result_t    C02B2_OK on return (per-channel failures
 *                            are logged but not propagated).
 */
c02b2_result_t CanIf_RecoverPump(void);
#endif /* C02B2_CAN_IF_H */
