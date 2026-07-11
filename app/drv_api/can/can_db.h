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
 * @details Called from the RX tick for every received frame whose
 *          can_id matches an entry in `can_msg_descs_ipk`.
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
 * @details Inverse of the s_dbc_to_bus[] table in can_db.c.  The
 *          table is generated from the relative order of the two
 *          enums; this is the public read-only accessor.
 *
 * @param[in]  db_sig_id  CAN_DB_SIG_* enum value (>0)
 *
 * @return  signal_id_t  Matching SIG_CAN_* id, or SIG_INVALID if out of range
 */
signal_id_t CanDb_DbcSigToBus(u16 db_sig_id);

#ifdef __cplusplus
}
#endif

#endif /* C02B2_CAN_DB_H */