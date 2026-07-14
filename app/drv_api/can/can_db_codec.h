/**
 * @file    can_db_codec.h
 * @brief   CAN signal encoding / decoding primitives
 * @brief   CAN 信号编/解码原语
 *
 * Provides the bit-level primitives used to decode received frames
 * into signal values and to encode signal values into outgoing
 * payloads, as defined by Vector DBC files.
 *
 * Two byte orders are supported:
 *   - Motorola / big-endian (DBC @0+) -- the payload is read MSB-first
 *     across bytes.  `start_bit` is a sawtooth index of the MSB of
 *     the field, converted to a network bit position by
 *     `network = 8 * (start / 8) + (7 - start % 8)`.
 *   - Intel / little-endian (DBC @1+) -- `start_bit` is the network
 *     bit position of the LSB of the field, and bits within each byte
 *     run from LSB upward.
 *
 * The signal descriptor `can_sig_desc_t` and the message descriptor
 * `can_msg_desc_t` are shared with the AUTOGEN tables emitted by
 * `tools/dbc_parse.py` (see `can_db_<node>_gen.{h,c}`).
 *
 * Quantisation rule: signals cross the int32 signal bus as
 *   int32 = raw_value * factor + offset
 * and back as
 *   raw_value = (int32 - offset) / factor.
 *
 * @ingroup can
 *
 * Type conventions
 * ----------------
 * This module follows the C02-B2 rule "unsigned by default, signed
 * only when the value can be negative":
 *
 *   - `can_raw_t`                : `u32` (default raw container)
 *   - `can_raw_s_t`         : `s32` -- ONLY for the return value
 *                                  of CanDb_BitExtractSigned(); never
 *                                  use it to hold a raw bit field.
 *   - `CanDb_BitExtract / Encode`: take / return `can_raw_t` (u32)
 *   - `CanDb_DecodeSignal`        : returns `s32` -- the decoded *physical* (raw stays on the signal bus; CanDb_DecodeSignal is only used by signal consumers that need a physical)
 *                                  physical value may be negative
 *                                  (signed DBC signal)
 *   - `CanDb_EncodeSignalValue`  : takes `s32` (physical input may
 *                                  be negative); returns `can_raw_t`
 *                                  for packing into the payload.
 *
 * All bit-level payloads and registers are `u8`-arrays; every signal
 * accessor uses unsigned types except when the physical value is
 * explicitly signed.
 */
#ifndef C02B2_CAN_DB_CODEC_H
#define C02B2_CAN_DB_CODEC_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------- *
 *  Raw value container type                                         *
 * ---------------------------------------------------------------- */

/**
 * @brief   Raw integer type sufficient to hold the largest signal
 *          bit width the codec supports (32 bits).
 * @brief   能容纳编解码器支持的最大信号位宽（32 位）的整数类型
 */
typedef u32 can_raw_t;

/**
 * @brief   Signed counterpart of `can_raw_t` for sign-extended reads.
 * @brief   `can_raw_t` 对应的有符号类型, 用于读出时的符号扩展
 */
typedef s32 can_raw_s_t;

/**
 * @brief   Symbolic raw-type tag for self-documenting descriptors
 * @brief   信号描述符里用于自描述的原始类型标记
 *
 * Selects the appropriate `can_raw_t` container at decode/encode time.
 * This is what `tools/dbc_parse.py` writes into `can_sig_desc_t::raw_type`.
 */
typedef enum {
    CAN_RAW_U8  = 0,
    CAN_RAW_I8  = 1,
    CAN_RAW_U16 = 2,
    CAN_RAW_I16 = 3,
    CAN_RAW_U32 = 4,
    CAN_RAW_I32 = 5,
    CAN_RAW_U64 = 6,
    CAN_RAW_I64 = 7
} can_raw_type_t;
/* ---------------------------------------------------------------- *
 *  Signal descriptor (per-signal metadata, AUTOGEN-fed)             *
 * ---------------------------------------------------------------- */

/**
 * @brief   Per-signal metadata extracted from a DBC SG_ line.
 * @brief   从 DBC SG_ 行解析出来的单个信号元数据
 *
 * Fields map 1:1 to DBC attributes:
 *   - start_bit / length: bit position and width in the payload.
 *   - byte_order: 0 = Motorola (big-endian), 1 = Intel (little-endian).
 *   - is_signed: 0 = unsigned, 1 = signed (two's complement).
 *   - factor / offset: physical = raw * factor + offset.
 *   - raw_type: storage hint (U8/I8/U16/...) used by the codec.
 */
typedef struct {
    u16           start_bit;
    u8            length;
    u8            byte_order;   /**< 0 = Motorola / big-endian, 1 = Intel / little-endian */
    u8            is_signed;    /**< 0 = unsigned, 1 = signed */
    u8            _rsvd;        /**< explicit pad for alignment */
    float         factor;
    float         offset;
    can_raw_type_t raw_type;
} can_sig_desc_t;

/* ---------------------------------------------------------------- *
 *  Message descriptor (per-frame metadata, AUTOGEN-fed)            *
 * ---------------------------------------------------------------- */

/**
 * @brief   Per-message metadata extracted from a DBC BO_ line.
 * @brief   从 DBC BO_ 行解析出来的单条报文元数据
 *
 * `sig_index` points into the per-node signal descriptor array
 * (emitted by the DBC parser); `sig_count` is the number of
 * consecutive signals for this message.
 */
typedef struct {
    u32           can_id;
    u8            dlc;
    u8            _rsvd;
    u16           sig_index;
    u16           sig_count;
    u16           _rsvd2;
    const char   *name;
    const char   *tx_node;
    u8            is_tx;        /**< 1 = this node transmits, 0 = this node receives */
} can_msg_desc_t;
/* ---------------------------------------------------------------- *
 *  Bit-level primitives                                             *
 * ---------------------------------------------------------------- */

/**
 * @brief   Extract a raw unsigned value from a CAN payload
 * @brief   从 CAN 报文 payload 中提取原始无符号值
 *
 * @details 从 `start_bit` 起沿 `byte_order` 隐含的方向遍历 `length` 个位。
 *          始终返回无符号值；若信号为二进制补码，
 *          请改用 `CanDb_BitExtractSigned`。
 *
 *          `length` 必须为 1..32。`start_bit` 遵循 Vector
 *          DBC 约定：
 *            Motorola：字段 MSB 的锯齿下标
 *            Intel：    字段 LSB 的网络位位置
 *
 * @param[in]  data        Pointer to at least 8 bytes of payload
 * @param[in]  start_bit   DBC start_bit (see above)
 * @param[in]  length      Bit width (1..32)
 * @param[in]  byte_order  0 = Motorola, 1 = Intel
 *
 * @return  can_raw_t  Zero-extended raw value
 */
can_raw_t CanDb_BitExtract(const u8 *data, u16 start_bit, u8 length, u8 byte_order);

/**
 * @brief   Like CanDb_BitExtract, but sign-extend to 32 bits.
 * @brief   同 CanDb_BitExtract, 但在末尾做符号扩展到 32 位
 *
 * @param[in]  data        Pointer to at least 8 bytes of payload
 * @param[in]  start_bit   DBC start_bit
 * @param[in]  length      Bit width (1..32)
 * @param[in]  byte_order  0 = Motorola, 1 = Intel
 *
 * @return  can_raw_s_t  Sign-extended raw value (two's complement)
 */
can_raw_s_t CanDb_BitExtractSigned(const u8 *data, u16 start_bit, u8 length, u8 byte_order);

/**
 * @brief   Encode an unsigned raw value into a CAN payload
 * @brief   将无符号原始值写入 CAN 报文 payload
 *
 * @details 字段之外的位保持不变（采用 read-modify-write）。
 *          `length` 必须为 1..32。
 *
 * @param[out] data        Payload buffer (8 bytes)
 * @param[in]  start_bit   DBC start_bit
 * @param[in]  length      Bit width (1..32)
 * @param[in]  byte_order  0 = Motorola, 1 = Intel
 * @param[in]  value       Raw value (only low `length` bits used)
 */
void CanDb_BitEncode(u8 *data, u16 start_bit, u8 length, u8 byte_order, can_raw_t value);

/* ---------------------------------------------------------------- *
 *  Signal-level helpers (apply factor/offset, hit signal bus)      *
 * ---------------------------------------------------------------- */

/**
 * @brief   Decode a signal descriptor's field from a payload into
 *          the raw u32 bit pattern on the signal bus.
 * @brief   从 payload 中按信号描述符解析字段, 转换成 int32 信号总线表示
 *
 *          Output = raw * factor + offset, rounded to nearest int32.
 *          Caller is responsible for clamping to signal min/max.
 *
 * @param[in]  data  8-byte payload (Intel or Motorola)
 * @param[in]  sig   Signal descriptor (start/length/order/signed/factor/offset)
 *
 * @return  s32  Quantised physical value (raw * factor + offset)
 */
s32 CanDb_DecodeSignal(const u8 *data, const can_sig_desc_t *sig);

/**
 * @brief   Extract a signal as a RAW (un-decoded) value from a payload.
 * @brief   从 payload 中按信号描述符抽取信号的 RAW(未解码)值
 *
 * @details 遵循 `sig->is_signed`。无符号信号返回
 *          零扩展后的原始位（CanDb_BitExtract）；
 *          有符号信号（DBC `-`）返回符号扩展后的原始位
 *          （CanDb_BitExtractSigned）。位模式保持不变，
 *          以便下游 Signal_Set(u32) 无损往返，
 *          并使 CanDb_DecodeSignal 能重新派生物理量。
 *
 *          这是 raw-on-the-bus 策略的调用点：
 *          信号总线值为原始位模式，物理量由
 *          各模块通过 CanDb_DecodeSignal(raw, sig) 自行计算。
 *
 * @param[in]  data  8-byte payload (Intel or Motorola)
 * @param[in]  sig   Signal descriptor (start/length/order/signed)
 *
 * @return  u32  Raw bit pattern cast back to unsigned
 */
u32 CanDb_GetRaw(const u8 *data, const can_sig_desc_t *sig);

/**
 * @brief   Decode a physical s32 (CanDb_DecodeSignal / Signal_Get on a non-CAN signal) into a u32 raw for the payload (rare -- physical already comes from DBC factor/offset; most callers use CanDb_EncodeSignal)
 *          that the CAN payload should carry.
 * @brief   把 int32 信号总线值转换为 CAN payload 应承载的原始值
 *
 *          raw = round((value - offset) / factor).
 *          Saturates to the signal's representable range (length bits).
 *
 * @param[in]  value  Physical value from Signal_Get
 * @param[in]  sig    Signal descriptor (factor/offset/length/signed)
 *
 * @return  can_raw_t  Raw value (will fit in `sig->length` bits)
 */
can_raw_t CanDb_EncodeSignalValue(s32 value, const can_sig_desc_t *sig);

/**
 * @brief   Pack a signal's raw value into a CAN payload buffer
 *          at the bit position defined by `sig`.
 * @brief   将信号的原始值按 sig 定义的位位置写入 CAN payload
 *
 * @param[out] data  8-byte payload buffer
 * @param[in]  sig   Signal descriptor (start/length/order)
 * @param[in]  raw   Raw value (truncated to `sig->length` bits)
 */
void CanDb_PackSignal(u8 *data, const can_sig_desc_t *sig, can_raw_t raw);

/**
 * @brief   Convenience: encode a physical s32 AND pack it
 *          into the payload.  Use ONLY when the caller has a s32 physical
 *          value (e.g. user formula result), NOT for raw-loopback. For
 *          raw already on the bus, prefer CanDb_PackSignal() /
 *          CanDb_BitEncode() to avoid the (value - offset) / factor round-trip.
 * @brief   便捷函数: 一步完成 s32 物理量 + payload 写入。仅在调用方
 *           手上有 s32 物理量(例如公式推导)时使用;raw-loopback 路径
 *           请用 CanDb_PackSignal / CanDb_BitEncode(避免 (value - offset)
 *           / factor 往返)。
 *
 * @param[out] data   8-byte payload buffer
 * @param[in]  sig    Signal descriptor
 * @param[in]  value  Physical value (bus-level, s32)
 */
void CanDb_EncodeAndPack(u8 *data, const can_sig_desc_t *sig, s32 value);

#ifdef __cplusplus
}
#endif

#endif /* C02B2_CAN_DB_CODEC_H */