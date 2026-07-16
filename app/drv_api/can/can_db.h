/**
 * @file    can_db.h
 * @brief   CAN database facade
 * @brief   CAN 数据库门面头
 *
 * Aggregates the DBC-driven CAN model used by the C02-B2 cluster.
 * The actual bit-level primitives and AUTOGEN tables live in:
 *   - can_db_codec.h   : BitExtract / BitEncode / Decode / Encode / Pack
 *   - can_db_<node>_gen.{h,c} : per-node tables emitted by
 *     tools/dbc_parse.py
 *
 * This header is a thin re-export and exposes the IPK test-batch
 * registration / dispatch entry points.  Legacy hand-written
 * g_can_rx_db[] / g_can_tx_db[] tables were removed once the DBC
 * pipeline became authoritative.
 */
#ifndef C02B2_CAN_DB_H
#define C02B2_CAN_DB_H

#include "can_db_codec.h"
#include "can_db_ipk_gen.h"   /* CAN_DB_IPK_*_COUNT macros + signal/message enums */
#include "signal.h"           /* signal_id_t for CanDb_DbcSigToBus */



/**
 * @brief   Apply per-signal timeout policy on OK->TIMED_OUT edge
 * @brief   在 OK->TIMED_OUT 边沿上,按 signal 级别 policy 执行超时动作
 *
 * @details v0.5: 给 IPK message index,回溯它的 sig_index/sig_count,
 *          走 SigTimeoutPolicy_Get() 判定 INIT_DBC / KEEP_LAST:
 *          - INIT_DBC 默认: 写 DBC init_value 进 slot。
 *          - KEEP_LAST: 不动 slot (保留 timeout 前最后一帧)。
 *          由 app/can/can_rx.c 50ms tick 在 OK->TIMED_OUT 边沿调用,
 *          业务方不应主动调用。
 *
 * @param[in]  ipk_msg_index  IPK message index (0..CAN_DB_IPK_MSG_COUNT-1)
 *
 * @return  c02b2_result_t
 * @retval  C02B2_OK            Policy applied
 * @retval  C02B2_ERR_PARAM     ipk_msg_index out of range
 */
c02b2_result_t CanDb_InvalidateSignalsOnMsgTimeout(u16 ipk_msg_index);

/**
 * @brief   Per-signal timeout policy: choose between writing the
 *          DBC-derived init value and keeping the last RX value.
 * @brief   每信号超时策略: 在写入 DBC init value 与保持上次 RX 值之间二选一。
 *
 * @details v0.4: 默认所有 signal 走 INIT_DBC(写 sig->init_value);
 *          CanDb_Init() 完成之后业务模块可以按需通过本 API 切换某 signal
 *          到 KEEP_LAST(保留 value)。切换时机与 prv_check_timeouts 边沿检测
 *          无竞争 (IAR Cortex-M 单核,policy 写后下次 tick 即生效)。
 *          保留枚举未来可扩展第三档 (SIG_TIMEOUT_ZERO 等)。
 */
typedef enum {
    SIG_TIMEOUT_INIT_DBC = 0u,  /**< 默认: 超时写 desc->init_value (DBC GenSigStartValue)*/
    SIG_TIMEOUT_KEEP_LAST = 1u, /**< 业务可选: 超时保留旧 value,仅清 valid (v0.3 行为)*/
} sig_timeout_policy_t;

/**
 * @brief   Override the per-signal timeout policy for one bus id.
 * @brief   设置单个 bus 上信号的超时策略。
 *
 * @details mcu_init 之后任何时刻都可调用;下次 OK→TIMED_OUT 边沿检测时即生效。
 *          业务举例: speed 信号需要保留最后有效帧供降级显示,可调
 *          CanDb_SetSignalTimeoutPolicy(SIG_CAN_<speed>, SIG_TIMEOUT_KEEP_LAST)。
 *
 * @param[in]  bus_id   Target signal bus id
 * @param[in]  policy   SIG_TIMEOUT_INIT_DBC 或 SIG_TIMEOUT_KEEP_LAST
 *
 * @return  c02b2_result_t
 * @retval  C02B2_OK             Policy set
 * @retval  C02B2_ERR_PARAM      bus_id 越界 (SIG_INVALID / out of range)
 */
c02b2_result_t CanDb_SetSignalTimeoutPolicy(signal_id_t bus_id,
                                            sig_timeout_policy_t policy);
#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------- *
 *  IPK test-batch aggregate                                         *
 *                                                                    *
 *  CAN_DB_IPK_MSG_COUNT / _RX_COUNT / _TX_COUNT / _SIG_COUNT are     *
 *  defined in can_db_ipk_gen.h (AUTOGEN).  This facade just re-exports
 *  the AUTOGEN descriptor tables and provides the runtime API.       *
 * ---------------------------------------------------------------- */

/**
 * @brief   Extern declarations for the AUTOGEN tables
 * @brief   AUTOGEN 表的外部声明
 */
extern const can_msg_desc_t  can_msg_descs_ipk[];          /**< IPK messages  (AUTOGEN) */
extern const can_sig_desc_t  can_sig_descs_ipk[];          /**< IPK signals   (AUTOGEN) */
extern const u16             can_db_ipk_rx_idx[];          /**< IPK RX indices (AUTOGEN) */
extern const u16             can_db_ipk_tx_idx[];          /**< IPK TX indices (AUTOGEN) */

/* ---------------------------------------------------------------- *
 *  High-level DBC dispatch                                          *
 * ---------------------------------------------------------------- */

/**
 * @brief   Decode every signal of `desc` out of `data` and publish
 *          the raw u32 bit-pattern via Signal_Set()
 * @brief   从 `data` 中按 `desc` 拆出每个信号, 并通过 Signal_Set
 *          发布到 int32 信号总线
 *
 * @details 由 RX tick 在每帧接收且 can_id 命中
 *          `can_msg_descs_ipk` 中的某项时调用。
 *
 * @param[in]  desc  Message descriptor (AUTOGEN, read-only)
 * @param[in]  data  8-byte payload (Intel or Motorola)
 */
void CanDb_DispatchByDb(const can_msg_desc_t *desc, const u8 *data);

/**
 * @brief   Find an IPK message descriptor by can_id.
 * @brief   按 can_id 查找 IPK 报文描述符
 *
 * @param[in]  can_id  Standard 11-bit can_id
 *
 * @return  const can_msg_desc_t*  Pointer into can_msg_descs_ipk[], or NULL
 */
const can_msg_desc_t *CanDb_FindIpkById(u32 can_id);

/**
 * @brief   Find an IPK signal descriptor by enum id.
 * @brief   按枚举 id 查找 IPK 信号描述符
 *
 * @param[in]  sig_id  CAN_DB_SIG_* enum value
 *
 * @return  const can_sig_desc_t*  Pointer into can_sig_descs_ipk[], or NULL
 */
const can_sig_desc_t *CanDb_FindIpkSig(u16 sig_id);

/**
 * @brief   Translate a DBC signal enum id to the corresponding
 *          signal-bus id (SIG_CAN_<Name> in app/signal/signal.h).
 * @brief   把 DBC 信号枚举 id 转换为 signal bus 上的对应 id
 *
 * @details 为 can_db.c 中 s_dbc_to_bus[] 表的反向查找。
 *          该表由两个枚举的相对顺序自动生成；
 *          本函数为其对外只读访问入口。
 *
 * @param[in]  db_sig_id  CAN_DB_SIG_* enum value (>0)
 *
 * @return  signal_id_t  Matching SIG_CAN_* id, or SIG_INVALID if out of range
 */
signal_id_t CanDb_DbcSigToBus(u16 db_sig_id);

/**
 * @brief   把 signal-bus id 解析为它在 RX 超时位图里的 bit-N。
 *
 * @param[in]  bus_id  SIG_CAN_* id（1..SIG_MAX-1）或 SIG_INVALID
 *
 * @retval  [0, 95]        所属 MSG 在 s_bit_to_can_id[] 中的 bit-N
 * @retval  sentinel_unused 该 bus_id 不映射到任何 IPK RX MSG
 *                          （TX signal / CAN health / 越界）
 */
u8 CanDb_SigToTimeoutBit(signal_id_t bus_id);

#ifdef __cplusplus
}
#endif

#endif /* C02B2_CAN_DB_H */