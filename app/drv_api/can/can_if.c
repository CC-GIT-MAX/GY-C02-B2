/**
 * @file    can_if.c
 * @brief   CAN interface implementation
 *
 * Bridges the app/can[/] layer to the vendor flexcan_driver.
 * Only this file is allowed to include "flexcan_driver.h".
 *
 * Data flow (RX):
 *   flexcan ISR  -> enqueue to ring buffer (lock-free single producer)
 *   can_rx tick  -> dequeue, dispatch via can_msg_t::cb
 *
 * Data flow (TX):
 *   can_tx tick  -> fill payload via pack() -> CanIf_Send()
 */
#include "can_if.h"

#include "sdk_project_config.h"
#include "flexcan_driver.h"
#include "interrupt_manager.h"

#define MOD_NAME  "CIF "
#include "log.h"
#include "signal.h"
#include "can_db.h"           /* extern can_msg_descs_ipk[]                   */
#include "can_db_ipk_gen.h"   /* CAN_DB_IPK_MSG_COUNT / _RX_COUNT etc.       */

#include "osif.h"           /* OSIF_GetMilliseconds for busy-warn dedup */
/* Phase 3 / C2: ack. 实测无反向依赖：drv_api/can/*.c 不 include 任何 app/can/can_*.h；app/can/can_{rx,tx}.c 反向 include drv_api/can/can_{if,db}.h 是合法分层（业务层 -> 驱动层）。文件头注释提到"被 app/can 引用"指的是 linker 符号解析，不是 include 反向。Marker closed. */
/* REVIEW: A1 SPSC ring 内存序未硬化（Phase 2 抽取中性 spsc 头文件） */
/* REVIEW: C6 volatile Pa082 兜底在不同工具链上脆弱（Phase 2 紧随 A1） */
/* Phase 1 / A5: ack. CanIf_Send 内已实现日志去重 + 冷却窗口 (s_tx_busy_warn_ms[] + CAN_TX_BUSY_WARN_COOLDOWN_MS). Marker closed. */
/* Phase 1 / A10 + B7: ack. CAN_RX_FILTER_ELEMS_PUBLIC/PRIVATE 已常量化 (RFFN=8->72, RFFN=6->56), #error 锁死 7+RFFN*2 不变量. Marker closed. */
/* REVIEW: A2 CanIf_Init 时序契约脆弱（Phase 4 增加 ready 门控） */

/* -------------------------------------------------------------------- *
 *  Compile-time sizing
 * -------------------------------------------------------------------- */
#define CAN_RX_RING_SIZE         32u   /* RX ring slots (double of 16 to absorb a full 5 ms tick at 64-ID load) */
#define CAN_RX_FILTER_ELEMS_PUBLIC  72u   /* RFFN=8 -> 8*9 = 72 */
/* Phase 1 / A10：钉死过滤表尺寸。
 * 厂商 SDK 未导出 RFFN -> 表大小的编译期常量；
 * FLEXCAN_DRV_ConfigRxFifo 运行期按 7 + RFFN*2 项使用。
 * 本工程 FlexCAN 配置硬编码 public 总线 RFFN=8（CTRL2.RFFN），
 * private 总线 RFFN=6；每个表项最多容纳 8 个 ID。
 * 若 SDK 后续提供常量，请把此处的字面量替换为对应宏。
 */
#if (CAN_RX_FILTER_ELEMS_PUBLIC != 72u)
#  error "A10: CAN_RX_FILTER_ELEMS_PUBLIC must match the RFFN=8 sizing (72 entries)"
#endif
#define CAN_RX_FILTER_ELEMS_PRIVATE 56u   /* RFFN=6 -> 8*7 = 56 */
#if (CAN_RX_FILTER_ELEMS_PRIVATE != 56u)
#  error "A10: CAN_RX_FILTER_ELEMS_PRIVATE must match the RFFN=6 sizing (56 entries)"
#endif

/* 各通道 FlexCAN RX-FIFO ID 过滤表。
 *  - s_rx_filter_public：  public CAN 总线（72 项，RFFN=8）
 *  - s_rx_filter_private：private CAN 总线（56 项，RFFN=6）
 * 用普通 static（而非 const），以便 FLEXCAN_DRV_ConfigRxFifo 遍历时
 * 不会受 volatile 修饰影响。 */
static flexcan_id_table_t s_rx_filter_public[CAN_RX_FILTER_ELEMS_PUBLIC];
static flexcan_id_table_t s_rx_filter_private[CAN_RX_FILTER_ELEMS_PRIVATE];

/* -------------------------------------------------------------------- *
 *  Ring buffer of received frames
 * -------------------------------------------------------------------- */

typedef struct {
    can_msg_t slot[CAN_RX_RING_SIZE];
    volatile u8 head;   /* producer (ISR) writes here                */
    volatile u8 tail;   /* consumer (tick) reads here                */
} can_rx_ring_t;

static can_rx_ring_t s_rx_ring;

/** Reusable scratch buffer used when arming single-MB RX. */
static flexcan_msgbuff_t s_rx_arm_buf;

/** Reusable scratch buffer used to kick off RX-FIFO reception.
 *  The buffer is handed to FLEXCAN_DRV_RxFifo() once at init;
 *  the driver uses it as the destination for the first frame,
 *  then replaces it on every FLEXCAN_EVENT_RXFIFO_COMPLETE. */
static flexcan_msgbuff_t s_rx_fifo_start_buf;

/** Tracks whether CanIf_Init has already been processed. The
 *  install-callback work must run exactly once per boot. */
static bool s_if_inited = false;

/**
 * @brief   Lock-free single-producer push; drops frame on overflow.
 * @brief   无锁单生产者入队；环满时丢弃最新帧
 *
 * @details 生产者（ISR）写入后递增 `head`。
 *          当 `head + 1 == tail` 时缓冲区为满（保留一个空位
 *          以区分满与空）。
 *
 * @param[in]  m  Frame to push
 *
 * @return  bool
 * @retval  true   Frame accepted
 * @retval  false  Ring full (frame dropped)
 */
static bool prv_ring_push(const can_msg_t *m)
{
    /* 计算下一个写入位置，环绕处理。 */
    u8 next = (u8)((s_rx_ring.head + 1u) % CAN_RX_RING_SIZE);
    /* 满检查：保留一个空位以区分满与空。 */
    if (next == s_rx_ring.tail) {
        return false;  /* full - silently drop, ISR cannot block */
    }
    /* 在发布 head 之前写入帧。（release barrier 见下文）
     *
     * 仅靠 volatile 顺序约束在带未来 D-cache 或 SMP 的 Cortex-M33 上
     * 并不足够：每次发布槽位都要配对 __DMB()，以确保消费者
     * （运行在更低优先级的 tick 或另一核上）不会先看到 head++
     * 再看到槽位字节。
     */
    s_rx_ring.slot[s_rx_ring.head] = *m;
    __DMB();  /* Phase 2 / A1: release barrier between slot publish and head. */
    /* 向消费者发布：head 现指向下一个空闲槽位。 */
    s_rx_ring.head = next;
    return true;
}

/**
 * @brief   Lock-free single-consumer pop.
 * @brief   无锁单消费者出队
 *
 * @details 消费者（tick）读取后递增 `tail`。
 *          `head == tail` 表示空。
 *
 * @param[out]  m  Populated on success
 *
 * @return  bool
 * @retval  true   Frame popped into `m`
 * @retval  false  Ring empty
 */
static bool prv_ring_pop(can_msg_t *m)
{
    /* 将两个 volatile 下标快照到本地，再发出 acquire barrier。
     * 一旦加入 D-cache 或 SMP 移植，仅靠 volatile 的顺序
     * 已不足以保证：__DMB() 保证下一行的槽位读取能看见生产者
     * 在 __DMB() + head++ 之前写入的字节。每个字段恰好访问一次，
     * 以规避 Pa082（独立 volatile 读取）。
     */
    u8 head = s_rx_ring.head;
    u8 tail = s_rx_ring.tail;
    __DMB();  /* Phase 2 / A1: acquire barrier before consuming slot bytes. */
    /* 空检查：所有槽位消费完时 head 等于 tail。 */
    if (head == tail) {
        return false;  /* empty */
    }
    /* 读取帧，然后推进 tail（下一次迭代触发 acquire）。 */
    *m = s_rx_ring.slot[tail];
    s_rx_ring.tail = (u8)((tail + 1u) % CAN_RX_RING_SIZE);
    return true;
}

/* -------------------------------------------------------------------- *
 *  flexcan glue
 * -------------------------------------------------------------------- */

/**
 * @brief   Convert a vendor flexcan_msgbuff_t into a portable can_msg_t.
 * @brief   将 vendor 的 flexcan_msgbuff_t 转换为平台无关的 can_msg_t
 *
 * @details YTM32B1M flexcan 驱动在 FLEXCAN_DRV_Receive 返回时
 *          直接填充 mb->msgId、mb->dataLen 与 mb->data[]。
 *          硬件 CS 字为驱动内部使用，不应由应用层解析，
 *          故此处忽略 mb->cs，直接信任 SDK 填充字段。
 *          ide/rtr 报告为 0（标准帧 + 数据帧），这也是
 *          当前 IPK DBC 唯一使用的帧类型。
 *
 * @param[in]  mb   Vendor mailbox descriptor (filled by FLEXCAN_DRV_Receive)
 * @param[out] out  Portable frame descriptor to fill
 */
static void prv_extract_msg(const flexcan_msgbuff_t *mb, can_msg_t *out)
{
    out->id  = mb->msgId;
    out->ide = 0u;            /* IPK DBC uses 11-bit STD only */
    out->rtr = 0u;            /* IPK DBC uses data frames only */
    /* 将 DLC 钳制到可移植帧大小（经典 CAN 8 字节）。 */
    u32 dlc = (mb->dataLen <= 8u) ? mb->dataLen : 8u;
    out->dlc = (u8)dlc;
    /* 复制 payload 字节 [0..dlc)，其余部分填零。 */
    for (u32 i = 0; i < dlc; i++) {
        out->data[i] = mb->data[i];
    }
    for (u32 i = dlc; i < 8u; i++) {
        out->data[i] = 0u;
    }
}

/**
 * @brief   Map a logical channel to its flexcan instance index.
 * @brief   逻辑通道到 flexcan 实例编号的映射
 *
 * @details board/can_config.h 通过 private_can_INST /
 *          public_can_INST 宏定义实例编号。
 */
static u8 prv_logical_to_inst(can_channel_t ch)
{
    /* 与 board/can_config.h 的命名保持一致。 */
    return (ch == CAN_CH_PUBLIC) ? public_can_INST : private_can_INST;
}

/**
 * @brief   flexcan driver callback (runs in ISR context)
 * @brief   flexcan 驱动回调（运行于 ISR 上下文）
 *
 * @details RX-FIFO 接收完成时，将帧复制出来并推入无锁
 *          环形缓冲区。溢出时静默丢弃，
 *          因为 ISR 不能阻塞。
 *
 * @param[in]  instance  flexcan instance index
 * @param[in]  ev        Event type (RX/TX/error)
 * @param[in]  buffIdx   Mailbox index
 * @param[in]  state     Driver state (unused)
 */
static void prv_flexcan_cb(u8 instance,
                           flexcan_event_type_t ev,
                           u32 buffIdx,
                           flexcan_state_t *state)
{
    (void)state;
    switch (ev) {
        case FLEXCAN_EVENT_RXFIFO_COMPLETE: {
            /* RX-FIFO 接收完成。
             *
             * 走到这里时：
             *   1. SDK ISR 已调用 FLEXCAN_ReadRxFifo()，将数据读入
             *      state->mbs[RXFIFO].mb_message（仍指向
             *      &s_rx_fifo_start_buf，因为我们用同一指针再启动）。
             *      因此新帧就放在 s_rx_fifo_start_buf 里。
             *   2. SDK 已将 state->mbs[RXFIFO].state 设为 IDLE。
             *   3. SDK 返回时会再次检查状态；若仍为 IDLE，
             *      它会调用 FLEXCAN_CompleteRxMessageFifoData()，
             *      该函数会关闭 FRAME_AVAILABLE 中断，此后
             *      不会再收到任何帧。
             *
             * 因此必须在返回前调用 FLEXCAN_DRV_RxFifo() 将状态
             * 翻回 RX_BUSY。关键是要传入同一缓冲区
             * (&s_rx_fifo_start_buf)，而非栈上局部：否则 SDK
             * 会把 mb_message 改写为栈地址，下一次 ISR 会把
             * 下一帧写到那里。
             *
             * 在 FLEXCAN_DRV_RxFifo() 之后再读 s_rx_fifo_start_buf
             * 是安全的，因为 SDK 只在 ISR 内（FLEXCAN_ReadRxFifo）
             * 写缓冲区；arm 调用仅翻状态并重新开中断，
             * 不会动缓冲区内容。
             */
            {
                can_msg_t m;
                prv_extract_msg(&s_rx_fifo_start_buf, &m);
                /* 用同一缓冲区再启动，使 SDK 在后续 IRQ 中继续
                 * 将新帧写入此处。同时将状态翻回 RX_BUSY，
                 * 避免 SDK 调用 FLEXCAN_CompleteRxMessageFifoData()
                 * （该调用会屏蔽 FRAME_AVAILABLE 中断并停止 RX）。 */
                (void)FLEXCAN_DRV_RxFifo(instance, &s_rx_fifo_start_buf);
                if (!prv_ring_push(&m)) {
                    /* 环形缓冲已满 —— 丢弃帧，让 drain 计数自然体现。 */
                    LOG_W("rx ring full (inst=%u id=0x%X dropped)",
                          (unsigned)instance, (unsigned)m.id);
                }
            }
            break;
        }
        case FLEXCAN_EVENT_RXFIFO_WARNING: {
            /* 等待中的帧 >= 5 —— 可能总线忙或消费者（tick）
             * 饿死。每次警告事件记一次日志，便于操作员关联分析。 */
            LOG_W("rx fifo warning (inst=%u) - consumer slow?", (unsigned)instance);
            break;
        }
        case FLEXCAN_EVENT_RXFIFO_OVERFLOW: {
            /* 至少有一帧在我们读取前被硬件覆盖。这是严重错误：
             * 真实帧丢失。记日志并递增内部计数（后续补丁：
             * 发布到信号总线）。 */
            LOG_E("rx fifo OVERFLOW (inst=%u) - frame(s) lost!", (unsigned)instance);
            break;
        }
        case FLEXCAN_EVENT_RX_COMPLETE: {
            /* 单 MB RX（通过 CanIf_ConfigRxMb 配置的邮箱）。
             * buffIdx 即邮箱下标，从中取出帧。 */
            flexcan_msgbuff_t mb;
            if (FLEXCAN_DRV_Receive(instance, (u8)buffIdx, &mb) == STATUS_SUCCESS) {
                can_msg_t m;
                prv_extract_msg(&mb, &m);
                if (!prv_ring_push(&m)) {
                    LOG_W("rx ring full (inst=%u mb=%u id=0x%X dropped)",
                          (unsigned)instance, (unsigned)buffIdx,
                          (unsigned)m.id);
                }
                /* 再启动 MB，使下一次匹配继续写入此邮箱。 */
                (void)FLEXCAN_DRV_Receive(instance, (u8)buffIdx, &mb);
            }
            break;
        }
        case FLEXCAN_EVENT_TX_COMPLETE: {
            /* 轮询 TX MB 完成 —— 无需动作，下一次 CanIf_Send
             * 调用会通过 prv_pick_tx_mb() 选出一个空闲 MB。 */
            break;
        }
        default:
            /* 错误 / 唤醒由独立错误回调处理。 */
            break;
    }
}

/* -------------------------------------------------------------------- *
 *  Error callback (runs in ISR context)                                *
 *                                                                    *
 *  Subscribes to FLEXCAN_DRV_InstallErrorCallback.  The SDK calls    *
 *  us whenever ESR1 fires one of the interrupt bits (BUS_OFF_ENTER,  *
 *  BUS_OFF_DONE, TX_WARNING, RX_WARNING, BIT_ERROR, ERROR_OVERRUN,   *
 *  RAM_ECC).  We log the event and publish bus-health signals so     *
 *  consumers (diag, NM, meter telltales) can react.                  *
 * -------------------------------------------------------------------- */

/** Per-channel cumulative bus-off enter counter (incremented on each
 *  BUS_OFF_ENTER transition; not cleared on recovery so the total is
 *  preserved across recoveries). */
static u32 s_bus_off_count[CAN_CH_MAX] = { 0u, 0u };

/** Per-channel soft-recovery pending flag.
 *  Set by prv_flexcan_err_cb() in ISR context, drained by
 *  CanIf_RecoverPump() called from the mod_can_rx 5 ms tick.
 *  Holding recovery out of ISR context is mandatory because
 *  FLEXCAN_DRV_Init / ConfigRxFifo are not ISR-safe (they touch
 *  peripheral RAM, masks, and several NVIC bits). */
static volatile u8 s_recovery_pending[CAN_CH_MAX] = { 0u, 0u };

/** Per-channel soft-recovery counter (incremented each time we
 *  successfully re-arm a channel; never cleared, useful for SOC). */
static u32 s_recovery_count[CAN_CH_MAX] = { 0u, 0u };

/**
 * @brief   Map a flexcan instance index to our logical channel
 * @brief   按 flexcan 实例号映射到逻辑通道
 */
static can_channel_t prv_inst_to_logical(u8 instance)
{
    /* public_can_INST=2，private_can_INST=1；其它情况全部映射到 PUBLIC
     * （为未来实例预留的防御式默认值）。 */
    if (instance == private_can_INST) return CAN_CH_PRIVATE;
    return CAN_CH_PUBLIC;
}

/**
 * @brief   Mark a channel for soft-recovery (ISR-safe, no peripheral access)
 * @brief   标记一个通道待软恢复(ISR 安全, 不访问外设)
 */
static void prv_mark_recovery(can_channel_t ch)
{
    if ((u32)ch >= (u32)CAN_CH_MAX) { return; }
    s_recovery_pending[ch] = 1u;
}

/**
 * @brief   flexcan error callback (runs in ISR context)
 * @brief   flexcan 错误回调（运行于 ISR 上下文）
 *
 * @details 处理本集群实际关心的四类事件：
 *   - BUS_OFF_ENTER  -> 记错误日志，置 SIG_CAN_BUS_OFF=1，计数加一
 *   - BUS_OFF_DONE   -> 记信息日志，清 SIG_CAN_BUS_OFF=0
 *   - TX_WARNING     -> 记警告日志并附当前 TX 错误计数
 *   - RX_WARNING     -> 记警告日志并附当前 RX 错误计数
 *   - 其它           -> 记调试日志
 *
 * @param[in]  instance   flexcan instance index
 * @param[in]  eventType  which ESR1 bit fired
 * @param[in]  state      Driver state (unused)
 */
static void prv_flexcan_err_cb(u8 instance,
                               flexcan_error_event_type_t eventType,
                               flexcan_state_t *state)
{
    (void)state;
    const can_channel_t ch = prv_inst_to_logical(instance);

    /* 一次性读取 ESR1：含 TX/RX 错误计数及活动状态位。
     * 依据 YTM32B1M 参考手册，[TXERRCNT:24-31]，
     * [RXERRCNT:16-23]。 */
    const u32 esr1 = FLEXCAN_DRV_GetErrorStatus(instance);
    const u32 tx_err = (esr1 >> 24) & 0xFFu;
    const u32 rx_err = (esr1 >> 16) & 0xFFu;
    (void)Signal_Set(SIG_CAN_TX_ERR_CNT, tx_err);
    (void)Signal_Set(SIG_CAN_RX_ERR_CNT, rx_err);

    switch (eventType) {
        case FLEXCAN_BUS_OFF_ENTER_EVENT: {
            /* 控制器进入 bus-off：已停止参与总线活动。
             * 依据 CAN ISO 11898，控制器在此状态至少停留
             * 128 次连续 11 个隐性位出现后才会触发 BUS_OFF_DONE。
             * 软件无法在此处直接恢复，只能置标志。 */
            s_bus_off_count[ch]++;
            (void)Signal_Set(SIG_CAN_BUS_OFF, 1);
            (void)Signal_Set(SIG_CAN_BUS_OFF_COUNT,
                             s_bus_off_count[ch]);
            LOG_E("CAN%u BUS_OFF enter (tx_err=%u rx_err=%u, total=%u)",
                  (unsigned)instance,
                  (unsigned)tx_err, (unsigned)rx_err,
                  (unsigned)s_bus_off_count[ch]);
            break;
        }
        case FLEXCAN_BUS_OFF_DONE_EVENT: {
            /* 控制器已从 bus-off 恢复并回到总线。清除标志并排队
             * 一次软恢复，由 Scheduler 5 ms tick 重新启动 RX-FIFO
             * 并重装回调（bus-off 残留状态可能导致中断被屏蔽，
             * 重新启动是唯一安全路径）。 */
            (void)Signal_Set(SIG_CAN_BUS_OFF, 0);
            prv_mark_recovery(ch);
            LOG_W("CAN%u BUS_OFF done -> queuing soft-recovery",
                  (unsigned)instance);
            break;
        }
        case FLEXCAN_TX_WARNING_EVENT: {
            /* TX 错误计数越过警告阈值（96）。总线质量已下降
             * 但尚未进入 bus-off。当 TX 计数达到 127
             * （active-error 与 passive 边界），控制器停止发出 ACK；
             * 排队软恢复，将计数清零并重新启动 RX-FIFO。 */
            LOG_W("CAN%u TX warning (tx_err=%u)", (unsigned)instance,
                  (unsigned)tx_err);
            if (tx_err >= 127u) { prv_mark_recovery(ch); }
            break;
        }
        case FLEXCAN_RX_WARNING_EVENT: {
            /* RX 错误计数越过警告阈值（96）。原因同 TX warning：
             * 达到 127 时控制器进入 passive 状态，不会再上报错误；
             * 复位以清除卡住的错误计数。 */
            LOG_W("CAN%u RX warning (rx_err=%u)", (unsigned)instance,
                  (unsigned)rx_err);
            if (rx_err >= 127u) { prv_mark_recovery(ch); }
            break;
        }
        case FLEXCAN_BIT_ERROR_EVENT: {
            /* 一次 TX 尝试期间的位错误：控制器置位错误标志但
             * 未离开总线。排队软恢复，以便后续 BUS_OFF 转换时
             * 控制器处于干净状态。 */
            LOG_W("CAN%u bit error (tx_err=%u rx_err=%u)",
                  (unsigned)instance, (unsigned)tx_err, (unsigned)rx_err);
            prv_mark_recovery(ch);
            break;
        }
        case FLEXCAN_ERROR_OVERRUN_EVENT: {
            /* 单 MB 的 RX 溢出 —— MB 在软件读取前被覆盖。
             * 在 FIFO 模式 + 32 帧环形缓冲下不应发生，若发生则
             * 记日志并排队软恢复以重启 FIFO（否则 FIFO 永久
             * 锁死会阻塞后续 RX）。 */
            LOG_E("CAN%u overrun - frame(s) lost on MB",
                  (unsigned)instance);
            prv_mark_recovery(ch);
            break;
        }
#if FEATURE_CAN_HAS_RAM_ECC
        case FLEXCAN_RAM_ECC_ERROR_EVENT: {
            /* CAN RAM ECC 错误 —— 硬件故障。记日志并触发
             * 通过软恢复执行完整控制器重新初始化。 */
            LOG_E("CAN%u RAM ECC error - controller in fault",
                  (unsigned)instance);
            prv_mark_recovery(ch);
            break;
        }
#endif
        case FLEXCAN_WAKEUP_EVENT: {
            /* 从低功耗唤醒 —— 记 info 日志。 */
            LOG_I("CAN%u wakeup event", (unsigned)instance);
            break;
        }
        default:
            LOG_D("CAN%u error event=0x%X (tx_err=%u rx_err=%u)",
                  (unsigned)instance, (unsigned)eventType,
                  (unsigned)tx_err, (unsigned)rx_err);
            break;
    }
}

/* -------------------------------------------------------------------- *
 *  TX mailbox round-robin                                                *
 *                                                                     *
 *  Per YTM32B1M Table 18.20:                                          *
 *    public_can  RFFN=8 (72 filters) -> FIFO occupies MB 0..23         *
 *    private_can RFFN=6 (56 filters) -> FIFO occupies MB 0..19         *
 *                                                                     *
 *  MB 26..31 are reserved for TX on both channels and are polled       *
 *  round-robin (next available MB wins).  The cursor is per-channel   *
 *  so PUBLIC and PRIVATE never collide on the same hardware MB.       *
 * -------------------------------------------------------------------- */
#define CAN_TX_MB_FIRST  26u   /**< First TX mailbox (inclusive)        */
#define CAN_TX_MB_LAST   31u   /**< Last TX mailbox (inclusive)         */
#define CAN_TX_MB_COUNT   6u   /**< Number of TX mailboxes for round-robin */

/** Per-channel round-robin cursor into the 26..31 TX mailbox window. */
static u8 s_tx_cursor[CAN_CH_MAX] = { CAN_TX_MB_FIRST, CAN_TX_MB_FIRST };

/* "6 个 TX MB 全忙" 警告的冷却。can_tx 轮询在冷启动排
 * 出积压帧时可能触及邮箱上限；每个时间窗内最多告警一次，
 * 保持日志可读。 */
#define CAN_TX_BUSY_WARN_COOLDOWN_MS  200u
static u32 s_tx_busy_warn_ms[CAN_CH_MAX] = { 0u, 0u };

/**
 * @brief   Pick the next TX mailbox via round-robin
 * @brief   通过轮询选取下一个发送邮箱
 *
 * @details 按顺序遍历 MB 26..31，返回首个驱动状态为
 *          FLEXCAN_MB_IDLE（即当前未在发送）的邮箱。
 *          六个邮箱都忙时返回 0xFFu，由调用方上报 ERR_BUSY。
 *
 *          起始游标为该通道最近一次成功 TX 的邮箱
 *          （s_tx_cursor[ch]），保证负载均匀分布，
 *          并优先选择刚发送完成的邮箱用于下一帧。
 *
 * @param[in]  inst  flexcan instance index (for status query)
 * @param[in]  ch    Logical channel (advances its cursor on success)
 *
 * @return  u8  Selected mailbox index (26..31), or 0xFFu if all busy
 */
static u8 prv_pick_tx_mb(u8 inst, can_channel_t ch)
{
    u8 start = s_tx_cursor[ch];
    for (u8 off = 0; off < CAN_TX_MB_COUNT; off++) {
        u8 mb = (u8)(CAN_TX_MB_FIRST +
                     ((start - CAN_TX_MB_FIRST + off) % CAN_TX_MB_COUNT));
        if (FLEXCAN_DRV_GetTransferStatus(inst, mb) != STATUS_BUSY) {
            s_tx_cursor[ch] = (u8)(mb + 1u);
            if (s_tx_cursor[ch] > CAN_TX_MB_LAST) {
                s_tx_cursor[ch] = CAN_TX_MB_FIRST;
            }
            return mb;
        }
    }
    return 0xFFu;  /* all six TX mailboxes busy */
}

/* -------------------------------------------------------------------- *
 *  Public API
 * -------------------------------------------------------------------- */

/**
 * @brief   Initialize both CAN channels and the RX ring buffer
 * @brief   初始化两条 CAN 通道及接收环形缓冲
 *
 * @details 将环形缓冲区初始化为空，然后对每个通道调用
 *          FLEXCAN_DRV_Init + InstallEventCallback。
 *          任何一步失败时立即返回 C02B2_ERR，
 *          部分初始化的通道将处于未定义状态。
 *
 * @return  c02b2_result_t
 * @retval  C02B2_OK  Both channels up
 * @retval  C02B2_ERR At least one FLEXCAN_DRV_Init failed
 */
void CanIf_InstallFlexcanCallbacks(void)
{
    for (u32 ch = 0; ch < CAN_CH_MAX; ch++) {
        const u8 inst = prv_logical_to_inst((can_channel_t)ch);
        FLEXCAN_DRV_InstallEventCallback(inst, prv_flexcan_cb, NULL);
        FLEXCAN_DRV_InstallErrorCallback(inst, prv_flexcan_err_cb, NULL);
    }
}

c02b2_result_t CanIf_ArmRxFifo(void)
{
    /* 关键：RX-FIFO 模式下，SDK 不会在 FLEXCAN_DRV_Init 期间
     * 启用 FRAME_AVAILABLE / WARNING / OVERFLOW 中断，
     * 真正的开启在 FLEXCAN_StartRxMessageFifoData 内（即
     * FLEXCAN_DRV_RxFifo 背后的实现）。因此首次调用负责启动
     * FIFO；事件回调中的后续调用则重新启动以接收下一帧。
     * 缺少首次调用会让控制器保持在线但 3 个 RX-FIFO 中断
     * 全部屏蔽，表现为"总线已起，分析仪在发帧，
     * 但应用层始终不见 RX 回调"。 */
    for (u32 ch = 0; ch < CAN_CH_MAX; ch++) {
        const u8 inst = prv_logical_to_inst((can_channel_t)ch);
        const status_t r = FLEXCAN_DRV_RxFifo(inst, &s_rx_fifo_start_buf);
        if (r != STATUS_SUCCESS) {
            LOG_E("RxFifo prime failed inst=%u (%d)", (unsigned)inst, (int)r);
            return C02B2_ERR;
        }
    }
    return C02B2_OK;
}

/**
 * @brief   Run a single channel through the full re-init sequence.
 * @brief   对一个通道跑一次完整的重新初始化。
 *
 * @details 与 Can_Init() 对应，但只针对单个实例。必须
 *          在 ISR 上下文外执行（FLEXCAN_DRV_Init / ConfigRxFifo /
 *          InstallEventCallback 会访问外设 RAM、屏蔽位
 *          与 NVIC 位）。由 CanIf_RecoverPump() 调用，后者
 *          运行在 mod_can_rx 5 ms tick 中（调度器因此保持 CAN 无关）。
 *
 * @param[in]  ch  Logical channel to recover
 */
/* 前向声明：prv_fill_filter_public 在文件中靠后定义，
 * 但 prv_do_recovery 会调用它，且源码顺序更靠前。
 * 缺少这些声明会在恢复调用点触发 Pe223（隐式声明）
 * 与 Pe020（s_rx_filter_* 未定义）。 */
static void prv_fill_filter_public(void);

static void prv_do_recovery(can_channel_t ch)
{
    const u8 inst = prv_logical_to_inst(ch);
    /* 1. 干净关闭控制器：禁用所有 FlexCAN 中断，
     *    进入 Freeze 模式，再关闭模块。在同一实例上第二次
     *    调用 FLEXCAN_DRV_Init 之前必须完成此步骤 —— SDK
     *    不会重置自身状态机，前一次的 NVIC / RAM 内容会
     *    泄漏到新会话。 */
    (void)FLEXCAN_DRV_Deinit(inst);
    /* 2. 完整重新初始化 —— 清除 BUS_OFF、错误计数、邮箱状态
     *    以及残留的中断屏蔽。 */
    if (ch == CAN_CH_PUBLIC) {
        FLEXCAN_DRV_Init(public_can_INST, &public_can_State, &public_can);
    } else {
        FLEXCAN_DRV_Init(private_can_INST, &private_can_State, &private_can);
    }
    /* 3. 重新填充 ID 过滤表 —— FLEXCAN_DRV_Init 会清空 RAM。 */
    prv_fill_filter_public();
    (void)FLEXCAN_DRV_ConfigRxFifo(public_can_INST,
                                   FLEXCAN_RX_FIFO_ID_FORMAT_A,
                                   s_rx_filter_public);
    (void)FLEXCAN_DRV_ConfigRxFifo(private_can_INST,
                                   FLEXCAN_RX_FIFO_ID_FORMAT_A,
                                   s_rx_filter_private);
    /* 4. 重新安装事件与错误回调（Init 时 SDK 会清空）。 */
    FLEXCAN_DRV_InstallEventCallback(inst, prv_flexcan_cb, NULL);
    FLEXCAN_DRV_InstallErrorCallback(inst, prv_flexcan_err_cb, NULL);
    /* 5. 重新启动 RX-FIFO —— 控制器复位后，
     *    FRAME_AVAILABLE / WARNING / OVERFLOW 中断由此真正重启。 */
    (void)FLEXCAN_DRV_RxFifo(inst, &s_rx_fifo_start_buf);
    /* 6. 清除 pending 标志并递增累计计数。 */
    s_recovery_pending[ch] = 0u;
    s_recovery_count[ch]++;
    LOG_W("CAN%u soft-recovery done (total=%u)",
          (unsigned)inst, (unsigned)s_recovery_count[ch]);
}

/**
 * @brief   Drain pending soft-recoveries scheduled by the error ISR
 * @brief   排空错误 ISR 标记的待处理软恢复请求
 *
 * @details prv_flexcan_err_cb() 在 ISR 中置位各通道的 pending 标志；
 *          CanIf_RecoverPump() 在 mod_can_rx 5 ms tick 中调用，
 *          在 ISR 上下文外执行实际的
 *          FLEXCAN_DRV_Deinit + Init + RX-FIFO 重新预启动流程。
 *
 * @return  c02b2_result_t  Always C02B2_OK (per-channel failures
 *                          are logged but not propagated)
 */
c02b2_result_t CanIf_RecoverPump(void)
{
    for (u32 ch = 0; ch < (u32)CAN_CH_MAX; ch++) {
        if (s_recovery_pending[ch] != 0u) {
            prv_do_recovery((can_channel_t)ch);
        }
    }
    return C02B2_OK;
}

/**
 * @brief   Reset the application-layer RX ring buffer
 * @brief   重置应用层接收环
 *
 * @details 所有 FlexCAN 硬件初始化（FLEXCAN_DRV_Init、
 *          FIFO 过滤配置、回调安装、FIFO 预启动）
 *          均在 Can_Init() 中完成。本函数仅清零 RX 环形
 *          索引，保证调度器 tick 启动时队列已知为空。
 *          可重复调用。
 *
 * @return  c02b2_result_t
 * @retval  C02B2_OK  Always (ring reset is idempotent)
 *
 * @note    Phase 1 / A8: boot-time order matters. The vendor SDK
 *          patches CanIf_Init() AFTER BSP_Init() (clocks + pins + DMA) and
 *          Can_Init() (FlexCAN controller + filter bring-up). CanIf_Init()
 *          itself only resets the app-layer RX ring; running it before
 *          Can_Init() leaks reception into an un-primed FIFO. Keep this
 *          ordering contract documented; do not "optimize" it away.
 */
c02b2_result_t CanIf_Init(void)
{
    /* 所有 FlexCAN 硬件初始化（FLEXCAN_DRV_Init、FIFO 过滤
     * 配置、回调安装、FIFO 预启动）均在 Can_Init() 中完成。
     * 本函数刻意保持轻量：仅复位应用层 RX 环形缓冲区，
     * 使调度器 tick 启动时队列已知为空。
     * 可重复调用。 */
    s_rx_ring.head = 0u;
    s_rx_ring.tail = 0u;
    if (s_if_inited) {
        LOG_I("init (re-entry, ring reset only)");
    } else {
        s_if_inited = true;
        LOG_I("init OK (ring buffer armed; HW up via Can_Init())");
    }
    return C02B2_OK;
}

/**
 * @brief   Register an RX callback (legacy stub)
 * @brief   注册一个 RX 回调（保留旧 API，无实际操作）
 *
 * @details 空操作，保留仅为源代码兼容性。
 *          实际路由表位于 can_db。
 *
 * @param[in]  ch     Logical channel
 * @param[in]  can_id CAN id (unused)
 * @param[in]  ide    0=STD, 1=EXT (unused)
 * @param[in]  cb     Callback (unused)
 *
 * @return  c02b2_result_t  Always C02B2_OK
 */
c02b2_result_t CanIf_RegisterRx(can_channel_t ch, u32 can_id, u8 ide, can_rx_cb_t cb)
{
    /* 已废弃的桩函数。仅保留以兼容 pre-DBC API；
     * 无运行时效果。RX 路由由 can_db.c（CanDb_DispatchByDb）
     * 在 ISR 填充的环形缓冲上遍历 can_msg_descs_ipk[] 完成。
     * 后续按 id 的过滤应在 DBC + can_db_ipk_gen 中表达，
     * 而非此钩子。 */
    (void)ch; (void)can_id; (void)ide; (void)cb;
    return C02B2_OK;
}

/**
 * @brief   Send a single CAN frame on the chosen channel
 * @brief   在指定通道上发送一帧 CAN 报文
 *
 * @details 将可移植的 can_msg_t 打包到厂商 flexcan_msgbuff_t，
 *          然后请求驱动通过保留邮箱发送
 *          （PUBLIC 用 MB 30，PRIVATE 用 MB 31）。
 *          若邮箱忙则丢弃该帧（不排队），
          以保持 tick 上下文调用非阻塞。
 *
 * @param[in]  ch   Logical channel
 * @param[in]  msg  Frame to transmit (id/ide/rtr/dlc/data)
 *
 * @return  c02b2_result_t
 * @retval  C02B2_OK        Accepted by the driver
 * @retval  C02B2_ERR_BUSY  Mailbox busy (frame dropped)
 */
c02b2_result_t CanIf_Send(can_channel_t ch, const can_msg_t *msg)
{
    u8 inst = prv_logical_to_inst(ch);
    /* 先将所有字段清零，再仅覆盖经典 CAN 实际需要的字段。
     * 若不清零，fd_enable / fd_padding / enable_brs 将携带
     * 栈上垃圾，使驱动把 CAN-FD 帧放到经典 CAN 总线上：
     * 分析仪看不到任何东西，驱动累计位错误，
     * 最终陷入 BUS_OFF / 恢复循环。
     * 使用 designated initializer，让枚举字段保持枚举类型
     * （规避 Pe188 —— 普通 = { 0 } 会触发该告警：
     * 0 是 int，字段类型为 flexcan_msg_id_type_t）。 */
    flexcan_data_info_t info = {
        .msg_id_type = FLEXCAN_MSG_ID_STD,
        .data_length = 0u,
        .fd_enable   = false,
        .fd_padding  = 0u,
        .enable_brs  = false,
        .is_remote   = false,
    };

    /* 选择 11-bit / 29-bit id 格式。msg->ide 为 u8 以保持可移植性；
     * 比较用条件表达式包裹以规避 Pe188（枚举与其它类型混合）。
     * 结果再赋给 info 的枚举字段。 */
    info.msg_id_type = (msg->ide != 0u) ? FLEXCAN_MSG_ID_EXT
                                        : FLEXCAN_MSG_ID_STD;
    info.data_length = msg->dlc;
    info.is_remote   = (msg->rtr != 0u);

    /* 从 MB 26..31 轮询选取。prv_pick_tx_mb() 在六个邮箱都忙时
     * 返回 0xFFu；此时上报 C02B2_ERR_BUSY，
     * 由调用方在下一次 tick 重试。 */
    u8 mb_idx = prv_pick_tx_mb(inst, ch);
    if (mb_idx == 0xFFu) {
        /* 邮箱池耗尽（冷启动积压、瞬时总线饱和）。
         * 每个通道在冷却窗口内最多记一次日志；can_tx 在下一轮
         * 重试该延迟帧。 */
        const u32 now_ms = OSIF_GetMilliseconds();
        if ((now_ms - s_tx_busy_warn_ms[ch]) >= CAN_TX_BUSY_WARN_COOLDOWN_MS) {
            s_tx_busy_warn_ms[ch] = now_ms;
            LOG_W("send id=0x%X all 6 TX MB busy (suppressed %ums)",
                  (unsigned)msg->id, (unsigned)CAN_TX_BUSY_WARN_COOLDOWN_MS);
        }
        return C02B2_ERR_BUSY;
    }
    /* FLEXCAN_DRV_Send 签名为 (inst, mb_idx, &info, msg_id, mb_data)：
     * 驱动从 tx_info 内部填写 MB CS 字（id 类型、dlc、remote 位）；
     * 一次性发送无需单独调用 FLEXCAN_DRV_ConfigTxMb。 */
    status_t r = FLEXCAN_DRV_Send(inst, mb_idx, &info, msg->id, msg->data);
    if (r != STATUS_SUCCESS) {
        /* Phase 1 / A5：对本警告去重，避免 bus-off 风暴时日志刷屏。 */
        const u32 _now_ms = OSIF_GetMilliseconds();
        if ((_now_ms - s_tx_busy_warn_ms[ch]) >= CAN_TX_BUSY_WARN_COOLDOWN_MS) {
            s_tx_busy_warn_ms[ch] = _now_ms;
            LOG_W("send id=0x%X failed (%d) (suppressed %ums)",
                  (unsigned)msg->id, (int)r,
                  (unsigned)CAN_TX_BUSY_WARN_COOLDOWN_MS);
        }
        return C02B2_ERR_BUSY;
    }
    return C02B2_OK;
}

/**
 * @brief   Globally enable/disable TX on a channel (stub)
 * @brief   全局使能/禁止某通道的发送（占位）
 *
 * @param[in]  ch  Channel
 * @param[in]  en  true = enable, false = disable
 *
 * @return  c02b2_result_t  Always C02B2_OK
 */
c02b2_result_t CanIf_SetTxEnabled(can_channel_t ch, bool en)
{
    (void)ch; (void)en;
    /* 当前 TX 始终启用。后续接入 CAN 静默 / 网络管理。 */
    return C02B2_OK;
}

/**
 * @brief   Request the channel to enter sleep mode (stub)
 * @brief   请求通道进入休眠模式（占位）
 *
 * @param[in]  ch  Channel
 *
 * @return  c02b2_result_t  Always C02B2_OK
 */
c02b2_result_t CanIf_GoToSleep(can_channel_t ch)
{
    (void)ch;
    /* TODO：进入 freeze 模式 + 收发器 STBY 引脚。 */
    return C02B2_OK;
}

/**
 * @brief   Wake the channel from sleep (stub)
 * @brief   将通道从休眠唤醒（占位）
 *
 * @param[in]  ch  Channel
 *
 * @return  c02b2_result_t  Always C02B2_OK
 */
c02b2_result_t CanIf_WakeUp(can_channel_t ch)
{
    (void)ch;
    /* TODO：退出 freeze，标记唤醒事件。 */
    return C02B2_OK;
}

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
bool CanIf_IsBusOff(can_channel_t ch)
{
    (void)ch;
    /* TODO：读取 CAN->ECR（错误计数寄存器）。 */
    return false;
}

/**
 * @brief   Get the cumulative bus-off recovery count (stub)
 * @brief   获取累计 bus-off 恢复次数（占位）
 *
 * @param[in]  ch  Channel
 *
 * @return  u32  Bus-off count (stub: always 0)
 */
u32 CanIf_GetBusOffCount(can_channel_t ch)
{
    (void)ch;
    return 0u;
}

/**
 * @brief   Pop one received frame from the ring
 * @brief   从环形缓冲弹出一帧接收报文
 *
 * @param[out] out  Populated on success
 *
 * @return  bool
 * @retval  true   Frame popped
 * @retval  false  Ring empty
 */
bool CanIf_PopRx(can_msg_t *out)
{
    return prv_ring_pop(out);
}

/**
 * @brief   Number of frames currently waiting in the RX ring
 * @brief   接收环中当前等待的报文数量
 *
 * @details 取模运算可正确处理 head >= tail（正常）
 *          与 head < tail（已环绕）两种情况。
 *          在取模前加 CAN_RX_RING_SIZE 可防止
 *          head < tail 时下溢。
 *
 * @return  u32  Count of pending frames
 */
u32 CanIf_RxPending(void)
{
    /* 快照两个 volatile 下标以规避 Pa082。 */
    u8 head = s_rx_ring.head;
    u8 tail = s_rx_ring.tail;
    /* 在取模前加 CAN_RX_RING_SIZE，防止 head < tail 时中间值为负。 */
    return (u32)((head + CAN_RX_RING_SIZE - tail) % CAN_RX_RING_SIZE);
}


/**
 * @brief   Return the post-FIFO mailbox layout for a channel
 * @brief   返回某通道 FIFO 配置之后的邮箱布局
 *
 * @details 从 board/can_config.c 硬编码而来：
 *          - public_can  : FIFO 0..23（72 个过滤器），RX 24..25，TX 26..31
 *          - private_can : FIFO 0..19（56 个过滤器），RX 20..25，TX 26..31
 *
 * @param[in]  ch  Logical channel
 *
 * @return  can_mb_layout_t  RX/TX mailbox windows for the caller
 */
can_mb_layout_t CanIf_GetMbLayout(can_channel_t ch)
{
    can_mb_layout_t lay;
    if (ch == CAN_CH_PUBLIC) {
        /* public_can：RFFN=8（72 个过滤器）-> FIFO MB 0..23 */
        lay.rx_mb_first = 24u;
        lay.rx_mb_last  = 25u;
        lay.tx_mb_first = CAN_TX_MB_FIRST;
        lay.tx_mb_last  = CAN_TX_MB_LAST;
    } else {
        /* private_can：RFFN=6（56 个过滤器）-> FIFO MB 0..19 */
        lay.rx_mb_first = 20u;
        lay.rx_mb_last  = 25u;
        lay.tx_mb_first = CAN_TX_MB_FIRST;
        lay.tx_mb_last  = CAN_TX_MB_LAST;
    }
    return lay;
}

/**
 * @brief   Configure a single RX mailbox with an exact CAN id
 * @brief   把单个接收邮箱配置为精确匹配某个 CAN id
 *
 * @details 封装 FLEXCAN_DRV_ConfigRxMb，使应用层无需了解
 *          FIFO 之后的邮箱窗口。邮箱下标必须位于
 *          CanIf_GetMbLayout() 返回的 rx_mb_first..rx_mb_last
 *          范围内；越界将被拒绝，以避免破坏
 *          FIFO ID 过滤表或 TX 轮询池。
 *
 * @param[in]  ch      Logical channel
 * @param[in]  mb_idx  Mailbox index
 * @param[in]  can_id  11-bit standard id to match
 * @param[in]  ide     0 = STD, 1 = EXT
 *
 * @return  c02b2_result_t
 * @retval  C02B2_OK         Mailbox configured and armed for RX
 * @retval  C02B2_ERR_PARAM  mb_idx outside the allowed RX window or
 *                           the SDK rejected the configuration
 */
c02b2_result_t CanIf_ConfigRxMb(can_channel_t ch, u8 mb_idx,
                                u32 can_id, u8 ide)
{
    const can_mb_layout_t lay = CanIf_GetMbLayout(ch);
    if (mb_idx < lay.rx_mb_first || mb_idx > lay.rx_mb_last) {
        LOG_W("ConfigRxMb: mb=%u outside RX window [%u..%u] on ch=%u",
              (unsigned)mb_idx,
              (unsigned)lay.rx_mb_first, (unsigned)lay.rx_mb_last,
              (unsigned)ch);
        return C02B2_ERR_PARAM;
    }
    const u8 inst = prv_logical_to_inst(ch);
    /* 使用 designated initializer 规避 Pe188（int 0 赋给枚举字段），
     * 同时显式将所有 boolean 字段置 false。与 CanIf_Send
     * 同样防御式：绝不信任 boolean 驱动标志上的栈垃圾。 */
    flexcan_data_info_t info = {
        .msg_id_type = FLEXCAN_MSG_ID_STD,
        .data_length = 8u,
        .fd_enable   = false,
        .fd_padding  = 0u,
        .enable_brs  = false,
        .is_remote   = false,
    };
    info.msg_id_type = (ide != 0u) ? FLEXCAN_MSG_ID_EXT : FLEXCAN_MSG_ID_STD;
    if (FLEXCAN_DRV_ConfigRxMb(inst, mb_idx, &info, can_id) != STATUS_SUCCESS) {
        LOG_E("ConfigRxMb failed inst=%u mb=%u id=0x%X",
              (unsigned)inst, (unsigned)mb_idx, (unsigned)can_id);
        return C02B2_ERR;
    }
    /* 依据 YTM32B1M 初始化流程：FLEXCAN_EnableRxFifo 会将
     * FIFO 表外每个 MB 的 RXIMR[i] 写为 0。这会将每个 MB 的
     * ID 屏蔽位清零，使到达该 MB 的任何帧在 CS 字 ID 比较
     * 之前就被过滤掉。ConfigRxMb 本身不会动 RXIMR，
     * 必须显式打开屏蔽位。对精确匹配使用"全 1"（每个 ID 位
     * 都必须与 CS 字 ID 匹配）。SetRxMaskType 保持默认 GLOBAL：
     * MB 下标 20..25 均 >= 14，无论 MRP 如何都会查询 RXIMR；
     * SetRxIndividualMask 在比较逻辑关注的位上能正确区分
     * STD 与 EXT。 */
    const flexcan_msgbuff_id_type_t id_type = (ide != 0u) ? FLEXCAN_MSG_ID_EXT
                                                          : FLEXCAN_MSG_ID_STD;
    const uint32_t all_ones = (ide != 0u) ? 0x1FFFFFFFu : 0x7FFu;
    if (FLEXCAN_DRV_SetRxIndividualMask(inst, id_type, mb_idx, all_ones)
            != STATUS_SUCCESS) {
        LOG_E("ConfigRxMb mask set failed inst=%u mb=%u",
              (unsigned)inst, (unsigned)mb_idx);
        return C02B2_ERR;
    }
    /* 启动 MB，使驱动在下次匹配时写入。 */
    if (FLEXCAN_DRV_Receive(inst, mb_idx, &s_rx_arm_buf) != STATUS_SUCCESS) {
        LOG_W("ConfigRxMb arm failed inst=%u mb=%u", (unsigned)inst, (unsigned)mb_idx);
    }
    LOG_I("ConfigRxMb ch=%u mb=%u id=0x%X ide=%u",
          (unsigned)ch, (unsigned)mb_idx, (unsigned)can_id, (unsigned)ide);
    return C02B2_OK;
}




/* ---------------------------------------------------------------- *
 *  DBC-aware convenience wrappers (see can_if.h for contract)
 *
 *  These forward to app/can/can_tx.c + can_rx.c so the caller
 *  (typically mod_can_demo or a diag module) only needs to include
 *  can_if.h.
 *
 *  The forward declarations below let us avoid pulling
 *  app/can/can_tx.h + can_rx.h into can_if.c -- the linker resolves
 *  them at link time.  Keep the signatures in lock-step with the
 *  real definitions.
 * ---------------------------------------------------------------- */
c02b2_result_t CanTx_PreparePayload(u32 can_id, const u8 *data, u8 dlc);
c02b2_result_t CanTx_EncodeSignal(u32 can_id, u16 sig_id, u32 raw);
c02b2_result_t CanTx_Trigger(u32 can_id);
c02b2_result_t CanRx_GetLastRawFrame(u32 can_id, can_msg_t *out);
u32 CanRx_GetRawFrameCount(void);

/**
 * @brief   Zero-fill an 8-byte TX payload buffer for an IPK can_id
 * @brief   按 IPK can_id 清零一个 8 字节 TX payload 缓冲区
 *
 * @details 为 demo / diag / 单元测试提供便利，
 *          先以全零 payload 发送一帧，
 *          随后通过 CanIf_TxEncodeSignal() 填充关注的信号。
 *
 * @param[in,out]  data  8-byte buffer to zero (existing contents ignored)
 * @param[in]      dlc   Data length code (0..8); 0 = 8 bytes
 * @param[in]      can_id  IPK standard can_id (must be in the TX table)
 *
 * @return  c02b2_result_t
 * @retval  C02B2_OK            Buffer zero-filled
 * @retval  C02B2_ERR_PARAM     data is NULL or can_id not in TX table
 */
c02b2_result_t CanIf_TxPreparePayload(u32 can_id, const u8 *data, u8 dlc)
{
    return CanTx_PreparePayload(can_id, data, dlc);
}

/**
 * @brief   Pack one DBC signal into an already-prepared TX payload
 * @brief   把一个 DBC 信号原始值写入已准备好的 TX payload
 *
 * @details 通过 CanDb_BitEncode() 将 `raw` 写入
 *          `can_id` 对应报文内 `sig_id` 所描述的位段。
 *          超出范围的 `raw` 值会被钳制到信号 length 位以内。
 *
 * @param[in]  can_id   IPK can_id (must be in the TX table)
 * @param[in]  sig_id   CAN_DB_SIG_* enum of the signal to write
 * @param[in]  raw      Raw bit pattern (will be masked to width)
 *
 * @return  c02b2_result_t
 * @retval  C02B2_OK            Signal written into payload
 * @retval  C02B2_ERR_PARAM     can_id or sig_id out of range
 */
c02b2_result_t CanIf_TxEncodeSignal(u32 can_id, u16 sig_id, u32 raw)
{
    return CanTx_EncodeSignal(can_id, sig_id, raw);
}

/**
 * @brief   Send a TX frame whose payload was prepared by
 *          CanIf_TxPreparePayload + CanIf_TxEncodeSignal calls
 * @brief   将已通过 CanIf_TxPreparePayload / EncodeSignal 准备好的
 *          payload 触发发送
 *
 * @details 将已准备的 payload 通过 CanIf_Send()
 *          在 `can_id` 隐含的通道上发出。供 demo / diag
 *          用来发送合成帧而无需持有 can_msg_t。
 *
 * @param[in]  can_id  IPK can_id (must be in the TX table)
 *
 * @return  c02b2_result_t
 * @retval  C02B2_OK            Frame accepted by driver
 * @retval  C02B2_ERR_BUSY      Reserved mailbox busy (frame dropped)
 * @retval  C02B2_ERR_PARAM     can_id not in TX table
 */
c02b2_result_t CanIf_TxTrigger(u32 can_id)
{
    return CanTx_Trigger(can_id);
}

/**
 * @brief   Convenience wrapper around CanRx_GetLastRawFrame
 * @brief   CanRx_GetLastRawFrame 的便利封装
 *
 * @details 返回自启动以来为 `can_id` 缓存的最新
 *          8 字节 payload。转发至 RX 侧缓存，
 *          便于 demo / diag / 单元测试直接查看原始帧，
 *          无需逐信号重新解码。
 *
 * @param[in]   can_id  IPK standard can_id
 * @param[out]  out     Populated on success
 *
 * @return  c02b2_result_t  See CanRx_GetLastRawFrame
 */
c02b2_result_t CanIf_RxGetLastRawFrame(u32 can_id, can_msg_t *out)
{
    return CanRx_GetLastRawFrame(can_id, out);
}

/**
 * @brief   Convenience wrapper around CanRx_GetRawFrameCount
 * @brief   CanRx_GetRawFrameCount 的便利封装
 *
 * @return  u32  Count of cached valid RX frames (0..CAN_DB_IPK_RX_COUNT)
 */
u32 CanIf_RxGetRawFrameCount(void)
{
    return CanRx_GetRawFrameCount();
}

/* ---------------------------------------------------------------- *
 *  Module-level bring-up (formerly app/drv_api/can/can_init.c)
 *
 *  Can_Init() drives the FlexCAN hardware bring-up in a single
 *  place so app/main.c / app/init/drv_init.c do not have to know
 *  about the SDK call sequence.
 *
 *  Bring-up sequence:
 *    1. FLEXCAN_DRV_Init              - reset MCR / RAM / masks
 *    2. FLEXCAN_DRV_ConfigRxFifo      - write ID filter tables
 *    3. CanIf_InstallFlexcanCallbacks - subscribe RX/TX/error events
 *    4. CanIf_ArmRxFifo              - enable FRAME_AVAILABLE/WARN/
 *                                       OVERFLOW IRQs, arm first RX
 * ---------------------------------------------------------------- */

/* Legacy FIFO ID 过滤表位于 FlexCAN RAM 偏移 0x18 处
 *（RxFifoFilterTableOffset）。每个元素 8 字节；
 * 表项数为 `RxFifoFilterElementNum(RFFN) = (RFFN + 1) * 8`，
 * 与 board/can_config.c 中的 num_id_filters 完全一致。
 *
 * 使用 Format A（每个元素含完整 29-bit ID + RTR + IDE 标志）。
 * 结合默认 RXFGMASK = 0xFFFFFFFF 和 RXIMR[i] = 0xFFFFFFFF
 *（由 FLEXCAN_Init 设置），FIFO 执行严格匹配接收：
 * 入站 ID 必须等于表项 ID（屏蔽位全 1）。
 *
 * 数组使用普通 static（而非 const），以便 FLEXCAN_DRV_ConfigRxFifo
 * 遍历时不会触发 volatile 限定符检查。 */
/** Populate s_rx_filter_public[] from can_msg_descs_ipk[].
 *  Each non-TX entry is copied into a free slot of the FIFO ID filter
 *  table (FORMAT_A). Standard frames only, RTR=0, IDE=0. Unused slots
 *  remain all-zero (which would match ID=0 with the default
 *  RXFGMASK=0xFFFFFFFF mask). */
static void prv_fill_filter_public(void)
{
    u32 n = 0u;
    for (u32 i = 0u; i < (u32)CAN_DB_IPK_RX_COUNT; i++) {
        const can_msg_desc_t *d = &can_msg_descs_ipk[i];
        if (d->is_tx != 0u) { continue; }   /* RX only */
        if (n >= (u32)CAN_RX_FILTER_ELEMS_PUBLIC) { break; }
        s_rx_filter_public[n].id              = d->can_id;
        s_rx_filter_public[n].isRemoteFrame   = false;
        s_rx_filter_public[n].isExtendedFrame = false;
        n++;
    }
}

/**
 * @brief   Initialize both FlexCAN instances
 * @brief   初始化两个 FlexCAN 实例
 *
 * @details 本板连接两条 CAN 通道：
 *   - 实例 1 = private 总线（车身 / 底盘域，板外传感器）
 *   - 实例 2 = public  总线（动力总成 / 信息娱乐，OBD 链路）
 *
 *          两者均保持 FLEXCAN_NORMAL_MODE（不回环），
 *          以便集群真正与整车其余部分通信。
 */
void Can_Init(void)
{
    /* 1. 控制器初始化 —— 复位 MCR / RAM / 屏蔽位，设置 RFFN。 */
    FLEXCAN_DRV_Init(1, &private_can_State, &private_can);
    FLEXCAN_DRV_Init(2, &public_can_State,  &public_can);

    /* 2. 写入 ID 过滤表。若不执行此步，Legacy FIFO 过滤表
     * 将保持启动时的残留值，不匹配的入站帧会被静默丢弃
     * （IFLAG1 永不置位，RX 回调不会触发）。 */
    prv_fill_filter_public();
    (void)FLEXCAN_DRV_ConfigRxFifo(1u, FLEXCAN_RX_FIFO_ID_FORMAT_A,
                                   s_rx_filter_private);
    (void)FLEXCAN_DRV_ConfigRxFifo(2u, FLEXCAN_RX_FIFO_ID_FORMAT_A,
                                   s_rx_filter_public);

    /* 3. 订阅驱动事件（RX / TX / 错误回调）。 */
    CanIf_InstallFlexcanCallbacks();

    /* 4. 启动 RX FIFO —— 该调用真正开启 RX-FIFO 模式下的
     * FRAME_AVAILABLE / WARNING / OVERFLOW 中断。
     * 若不调用，即使过滤表配置完美，也不会产生 IRQ。 */
    (void)CanIf_ArmRxFifo();
}
