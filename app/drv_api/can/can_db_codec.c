/**
 * @file    can_db_codec.c
 * @brief   CAN signal encoding / decoding primitives
 * @brief   CAN 信号编/解码原语
 *
 * Two byte orders are supported:
 *   - Motorola / big-endian (DBC @0+): the payload is read MSB-first
 *     across bytes.  `start_bit` is a "sawtooth" index -- it is the
 *     position of the MSB of the field, counted from the MSB of each
 *     byte going down toward LSB and then into the next byte.  The
 *     sawtooth index is converted to a network bit position by
 *     `network = 8 * (start / 8) + (7 - start % 8)`.
 *   - Intel / little-endian (DBC @1+): `start_bit` is the network bit
 *     position of the LSB of the field.  Bits within a byte are read
 *     from LSB upward.
 *
 * The two byte orders agree on the network bit numbering -- network
 * bit 0 is the MSB of byte 0, bit 7 is the LSB of byte 0, bit 8 is
 * the MSB of byte 1, ...  Bit-7-of-byte-N == network bit (8N+7).
 *
 * Performance: all paths are O(length) with no malloc; safe to call
 * from any context including CAN RX interrupt.
 */
#include "can_db_codec.h"

#define MOD_NAME  "CDBC"
#include "log.h"

/* ---------------------------------------------------------------- *
 *  Internal helpers                                                 *
 * ---------------------------------------------------------------- */

/**
 * @brief   Set the `n`-th bit (LSB-numbered) of `data` to `val`.
 * @brief   把 data 的第 n 位（LSB 编号）置为 val
 */
static inline void prv_set_bit_lsb(u8 *data, u8 n, u8 val)
{
    if (val) { data[n >> 3] |=  (u8)(1u << (n & 0x7u)); }
    else     { data[n >> 3] &=  (u8)~(1u << (n & 0x7u)); }
}

/**
 * @brief   Get the `n`-th bit (LSB-numbered) of `data`.
 * @brief   读取 data 的第 n 位（LSB 编号）
 */
static inline u8 prv_get_bit_lsb(const u8 *data, u8 n)
{
    return (u8)((data[n >> 3] >> (n & 0x7u)) & 0x1u);
}

/**
 * @brief   Set the `n`-th bit (MSB-numbered, "Motorola") of `data`.
 * @brief   把 data 的第 n 位（MSB 编号, Motorola 约定）置为 val
 *
 * Motorola bit numbering: byte index = n / 8, MSB within byte = 7 - (n % 8).
 */
static inline void prv_set_bit_msb(u8 *data, u8 n, u8 val)
{
    const u8 byte_idx = (u8)(n >> 3);
    const u8 bit_idx  = (u8)(7u - (n & 0x7u));
    if (val) { data[byte_idx] |=  (u8)(1u << bit_idx); }
    else     { data[byte_idx] &=  (u8)~(1u << bit_idx); }
}

/**
 * @brief   Get the `n`-th bit (MSB-numbered) of `data`.
 * @brief   读取 data 的第 n 位（MSB 编号）
 */
static inline u8 prv_get_bit_msb(const u8 *data, u8 n)
{
    const u8 byte_idx = (u8)(n >> 3);
    const u8 bit_idx  = (u8)(7u - (n & 0x7u));
    return (u8)((data[byte_idx] >> bit_idx) & 0x1u);
}

/* ---------------------------------------------------------------- *
 *  Public API                                                       *
 * ---------------------------------------------------------------- */

/**
 * @brief   Extract a raw unsigned value from a CAN payload
 * @brief   从 CAN 报文 payload 中提取原始无符号值
 *
 * @param[in]  data        Pointer to at least 8 bytes of payload
 * @param[in]  start_bit   DBC start_bit
 * @param[in]  length      Bit width (1..32)
 * @param[in]  byte_order  0 = Motorola, 1 = Intel
 *
 * @return  can_raw_t  Zero-extended raw value
 */
can_raw_t CanDb_BitExtract(const u8 *data, u16 start_bit, u8 length, u8 byte_order)
{
    can_raw_t value = 0u;
    if (length == 0u) { return 0u; }

    /* 在字段内按 MSB 在前遍历，使读取顺序与字节序无关：
     * 每步都将下一位移入结果的高位。 */
    if (byte_order == 1u) {
        /* Intel：start_bit 为字段 LSB 在网络位序中的位置，
         * 字段的 MSB 位于网络位 (start_bit + length - 1)。
         * 每个字节内按 LSB 升序读取，故调用 prv_get_bit_lsb()。 */
        for (u8 i = 0u; i < length; i++) {
            const u16 n = (u16)(start_bit + length - 1u - i);
            const u8  b = prv_get_bit_lsb(data, (u8)n);
            value = (can_raw_t)((value << 1) | (can_raw_t)b);
        }
    } else {
        /* Motorola：start_bit 为字段 MSB 的锯齿下标。
         * 先换算为 MSB 实际所在的网络位位置，
         * 再按 MSB 在前读取 `length` 个位
         * （网络 0 = byte 0 MSB，网络 7 = byte 0 LSB，
         *  网络 8 = byte 1 MSB，依此类推）。按 MSB 在前读取时，
         * 每字节内位下标每步递减一。 */
        const u16 msb_net = (u16)((u16)(start_bit & 0xFFF8u)
                                 + (u16)(7u - (start_bit & 0x7u)));
        for (u8 i = 0u; i < length; i++) {
            const u16 n = (u16)(msb_net + i);
            const u8  b = prv_get_bit_msb(data, (u8)n);
            value = (can_raw_t)((value << 1) | (can_raw_t)b);
        }
    }
    return value;
}
/**
 * @brief   Like CanDb_BitExtract, but sign-extend to 32 bits.
 * @brief   同 CanDb_BitExtract, 但在末尾做符号扩展到 32 位
 *
 * @param[in]  data        Pointer to at least 8 bytes of payload
 * @param[in]  start_bit   DBC start_bit
 * @param[in]  length      Bit width (1..32)
 * @param[in]  byte_order  0 = Motorola, 1 = Intel
 *
 * @return  can_raw_s_t  Sign-extended raw value
 */
can_raw_s_t CanDb_BitExtractSigned(const u8 *data, u16 start_bit, u8 length, u8 byte_order)
{
    const can_raw_t raw = CanDb_BitExtract(data, start_bit, length, byte_order);
    if (length == 0u || length >= 32u) {
        /* 符号位为 bit (length-1)；当 length==32 时已在 s32 内。 */
        return (can_raw_s_t)raw;
    }
    const can_raw_t sign_mask = (can_raw_t)1u << (length - 1u);
    if ((raw & sign_mask) != 0u) {
        /* 负数：将 `length` 之上的所有位置 1。 */
        const can_raw_t extend = ~(sign_mask) + 1u;  /* 算术 -1 << length 不具备可移植性 */
        const can_raw_t fill   = ~((can_raw_t)1u << length) + 1u;
        (void)extend;
        return (can_raw_s_t)(raw | fill);
    }
    return (can_raw_s_t)raw;
}

/**
 * @brief   Encode an unsigned raw value into a CAN payload
 * @brief   将无符号原始值写入 CAN 报文 payload
 *
 * @param[out] data        Payload buffer (8 bytes)
 * @param[in]  start_bit   DBC start_bit
 * @param[in]  length      Bit width (1..32)
 * @param[in]  byte_order  0 = Motorola, 1 = Intel
 * @param[in]  value       Raw value (only low `length` bits used)
 */
void CanDb_BitEncode(u8 *data, u16 start_bit, u8 length, u8 byte_order, can_raw_t value)
{
    if (length == 0u) { return; }
    if (byte_order == 1u) {
        /* Intel：将 `value` 的 LSB 写入 start_bit，再向高位递增。
         * 每个字节内按 LSB 升序写入。 */
        for (u8 i = 0u; i < length; i++) {
            const u16 n = (u16)(start_bit + i);
            const u8  b = (u8)((value >> i) & 0x1u);
            prv_set_bit_lsb(data, (u8)n, b);
        }
    } else {
        /* Motorola：先将锯齿 start_bit 换算为网络 MSB 位置，
         * 再向前逐位写入。后续位位于下一网络位置，
         * 跨字节时落到下一字节的 MSB。
         * `value` 的 MSB 落在该网络 MSB 位置上。 */
        const u16 msb_net = (u16)((u16)(start_bit & 0xFFF8u)
                                 + (u16)(7u - (start_bit & 0x7u)));
        for (u8 i = 0u; i < length; i++) {
            const u16 n = (u16)(msb_net + i);
            const u8  b = (u8)((value >> (length - 1u - i)) & 0x1u);
            prv_set_bit_msb(data, (u8)n, b);
        }
    }
}
/* ---------------------------------------------------------------- *
 *  Signal-level helpers (factor / offset / quantisation)           *
 * ---------------------------------------------------------------- */

/**
 * @brief   Decode a signal descriptor's field from a payload into
 *          the raw u32 bit pattern on the signal bus.
 * @brief   从 payload 中按信号描述符解析字段, 转换成 int32 信号总线表示
 *
 * @param[in]  data  8-byte payload
 * @param[in]  sig   Signal descriptor
 *
 * @return  s32  Quantised physical value (raw * factor + offset)
 */
u32 CanDb_GetRaw(const u8 *data, const can_sig_desc_t *sig)
{
    if (sig->is_signed) {
        /* DBC `-`：符号扩展；cast 到 u32 保留位模式。 */
        const s32 raw_s = (s32)CanDb_BitExtractSigned(data, sig->start_bit, sig->length, sig->byte_order);
        return (u32)raw_s;
    }
    return CanDb_BitExtract(data, sig->start_bit, sig->length, sig->byte_order);
}


/**
 * @brief   Decode one DBC signal from a payload to its physical value
 * @brief   从 payload 中按 DBC 信号描述符解码, 返回物理量
 *
* @details 遵循 `sig->is_signed`：有符号信号走
*          CanDb_BitExtractSigned()，先符号扩展原始位再
*          施加 factor/offset。输出为 `raw * factor + offset`，
*          四舍五入（half-away-from-zero）后截断到 s32。
 *
 * @param[in]  data  Pointer to at least 8 bytes of payload
 * @param[in]  sig   Signal descriptor (AUTOGEN, read-only)
 *
 * @return  s32  Quantised physical value (raw * factor + offset)
 */
s32 CanDb_DecodeSignal(const u8 *data, const can_sig_desc_t *sig)
{
    s32 raw;
    if (sig->is_signed) {
        raw = (s32)CanDb_BitExtractSigned(data, sig->start_bit, sig->length, sig->byte_order);
    } else {
        raw = (s32)CanDb_BitExtract(data, sig->start_bit, sig->length, sig->byte_order);
    }
    /* physical = raw * factor + offset，结果四舍五入到 int32 */
    const float f = (float)raw * sig->factor + sig->offset;
    /* 负数采用 half-away-from-zero：非负值 +0.5 再 cast 即正确；
     * 负值则需 -0.5 再 cast。 */
    if (f >= 0.0f) { return (s32)(f + 0.5f); }
    return (s32)(f - 0.5f);
}

/**
 * @brief   Decode a physical s32 (CanDb_DecodeSignal / Signal_Get on a non-CAN signal) into a u32 raw for the payload (rare -- physical already comes from DBC factor/offset; most callers use CanDb_EncodeSignal)
 *          that the CAN payload should carry.
 * @brief   把 int32 信号总线值转换为 CAN payload 应承载的原始值
 *
 * @param[in]  value  Physical value from Signal_Get
 * @param[in]  sig    Signal descriptor
 *
 * @return  can_raw_t  Raw value (fits in `sig->length` bits)
 */
can_raw_t CanDb_EncodeSignalValue(s32 value, const can_sig_desc_t *sig)
{
    /* raw = (value - offset) / factor，四舍五入 */
    float f;
    if (sig->factor == 0.0f) {
        /* factor 退化为 0 —— 直接输出整型值，避免除零
         * （某些粗制 DBC 会出现这种情况）。 */
        f = (float)value;
    } else {
        f = ((float)value - sig->offset) / sig->factor;
    }
    s32 raw;
    if (f >= 0.0f) { raw = (s32)(f + 0.5f); }
    else            { raw = (s32)(f - 0.5f); }

    /* 将结果饱和截断到当前 length 可表示的范围内。 */
    if (sig->is_signed) {
        if (sig->length == 0u || sig->length >= 32u) {
            return (can_raw_t)raw;
        }
        const s32 min_v = -(s32)((u32)1u << (sig->length - 1u));
        const s32 max_v = (s32)((u32)1u << (sig->length - 1u)) - 1;
        if (raw < min_v) raw = min_v;
        if (raw > max_v) raw = max_v;
    } else {
        if (sig->length == 0u) {
            return 0u;
        }
        const can_raw_t max_v = (sig->length >= 32u)
            ? (can_raw_t)0xFFFFFFFFu
            : (((can_raw_t)1u << sig->length) - 1u);
        if (raw < 0) raw = 0;
        if ((can_raw_t)raw > max_v) return max_v;
    }
    return (can_raw_t)raw;
}

/**
 * @brief   Pack a signal's raw value into a CAN payload buffer
 * @brief   将信号的原始值按 sig 定义的位位置写入 CAN payload
 *
 * @param[out] data  8-byte payload buffer
 * @param[in]  sig   Signal descriptor
 * @param[in]  raw   Raw value (truncated to `sig->length` bits)
 */
void CanDb_PackSignal(u8 *data, const can_sig_desc_t *sig, can_raw_t raw)
{
    CanDb_BitEncode(data, sig->start_bit, sig->length, sig->byte_order, raw);
}

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
void CanDb_EncodeAndPack(u8 *data, const can_sig_desc_t *sig, s32 value)
{
    const can_raw_t raw = CanDb_EncodeSignalValue(value, sig);
    CanDb_PackSignal(data, sig, raw);
}
