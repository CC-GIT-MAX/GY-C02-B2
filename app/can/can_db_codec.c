/**
 * @file    can_db_codec.c
 * @brief   CAN signal encoding / decoding primitives
 * @brief   CAN 信号编/解码原语
 *
 * Two byte orders are supported:
 *   - Intel / little-endian (DBC @0+): bytes laid out LSB first,
 *     within each byte the LSB is bit 0.
 *   - Motorola / big-endian (DBC @1+): bytes laid out MSB first,
 *     "start_bit" refers to the MSB of the field.
 *
 * Bit numbering convention follows Vector DBC v3 / CANdb++ docs:
 *   byte index = start_bit / 8
 *   Intel:   bit-within-byte (from LSB) = start_bit % 8
 *   Motorola:bit-within-byte (from MSB) = 7 - (start_bit % 8)
 *
 * Performance: all paths are O(length) with no malloc; safe to call
 * from any context including CAN RX interrupt.
 */
#include "can_db_codec.h"

#define LOG_NAME  "CDBC"
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
 * @param[in]  byte_order  0 = Intel, 1 = Motorola
 *
 * @return  can_raw_t  Zero-extended raw value
 */
can_raw_t CanDb_BitExtract(const u8 *data, u16 start_bit, u8 length, u8 byte_order)
{
    can_raw_t value = 0u;
    if (length == 0u) { return 0u; }

    /* Walk MSB-first within the field so the read order does not
     * matter for endianness: each step contributes the next bit
     * of the result shifted into the high end. */
    if (byte_order == 0u) {
        /* Intel: bit n is at data[n/8] bit (n%8) */
        for (u8 i = 0u; i < length; i++) {
            const u16 n = (u16)(start_bit + length - 1u - i);
            const u8  b = prv_get_bit_lsb(data, (u8)n);
            value = (can_raw_t)((value << 1) | (can_raw_t)b);
        }
    } else {
        /* Motorola: bit n is at data[n/8] bit (7 - n%8) */
        for (u8 i = 0u; i < length; i++) {
            const u16 n = (u16)(start_bit + length - 1u - i);
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
 * @param[in]  byte_order  0 = Intel, 1 = Motorola
 *
 * @return  can_raw_signed_t  Sign-extended raw value
 */
can_raw_signed_t CanDb_BitExtractSigned(const u8 *data, u16 start_bit, u8 length, u8 byte_order)
{
    const can_raw_t raw = CanDb_BitExtract(data, start_bit, length, byte_order);
    if (length == 0u || length >= 32u) {
        /* Sign bit is bit (length-1); for length==32 it already fits in s32. */
        return (can_raw_signed_t)raw;
    }
    const can_raw_t sign_mask = (can_raw_t)1u << (length - 1u);
    if ((raw & sign_mask) != 0u) {
        /* Negative: set all bits above `length`. */
        const can_raw_t extend = ~(sign_mask) + 1u;  /* arithmetic -1 << length not portable */
        const can_raw_t fill   = ~((can_raw_t)1u << length) + 1u;
        (void)extend;
        return (can_raw_signed_t)(raw | fill);
    }
    return (can_raw_signed_t)raw;
}

/**
 * @brief   Encode an unsigned raw value into a CAN payload
 * @brief   将无符号原始值写入 CAN 报文 payload
 *
 * @param[out] data        Payload buffer (8 bytes)
 * @param[in]  start_bit   DBC start_bit
 * @param[in]  length      Bit width (1..32)
 * @param[in]  byte_order  0 = Intel, 1 = Motorola
 * @param[in]  value       Raw value (only low `length` bits used)
 */
void CanDb_BitEncode(u8 *data, u16 start_bit, u8 length, u8 byte_order, can_raw_t value)
{
    if (length == 0u) { return; }
    if (byte_order == 0u) {
        for (u8 i = 0u; i < length; i++) {
            const u16 n = (u16)(start_bit + i);
            const u8  b = (u8)((value >> i) & 0x1u);
            prv_set_bit_lsb(data, (u8)n, b);
        }
    } else {
        for (u8 i = 0u; i < length; i++) {
            const u16 n = (u16)(start_bit + i);
            const u8  b = (u8)((value >> i) & 0x1u);
            prv_set_bit_msb(data, (u8)n, b);
        }
    }
}
/* ---------------------------------------------------------------- *
 *  Signal-level helpers (factor / offset / quantisation)           *
 * ---------------------------------------------------------------- */

/**
 * @brief   Decode a signal descriptor's field from a payload into
 *          the int32 signal-bus representation.
 * @brief   从 payload 中按信号描述符解析字段, 转换成 int32 信号总线表示
 *
 * @param[in]  data  8-byte payload
 * @param[in]  sig   Signal descriptor
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
    /* physical = raw * factor + offset, rounded to nearest int32 */
    const float f = (float)raw * sig->factor + sig->offset;
    /* round-half-away-from-zero for negative values: add 0.5 and cast
     * works correctly for non-negative; for negative we need -0.5 + cast. */
    if (f >= 0.0f) { return (s32)(f + 0.5f); }
    return (s32)(f - 0.5f);
}

/**
 * @brief   Convert an int32 signal-bus value into the raw value
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
    /* raw = (value - offset) / factor, rounded */
    float f;
    if (sig->factor == 0.0f) {
        /* Degenerate factor -- emit the integer value directly so we
         * never divide by zero (some sloppy DBCs do this). */
        f = (float)value;
    } else {
        f = ((float)value - sig->offset) / sig->factor;
    }
    s32 raw;
    if (f >= 0.0f) { raw = (s32)(f + 0.5f); }
    else            { raw = (s32)(f - 0.5f); }

    /* Saturate to the representable range for this length. */
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
 * @brief   Convenience: encode the int32 bus value AND pack it
 *          into the payload in one step.
 * @brief   便捷函数: 一步完成 int32 总线值的编码 + payload 写入
 *
 * @param[out] data   8-byte payload buffer
 * @param[in]  sig    Signal descriptor
 * @param[in]  value  Physical value (bus-level)
 */
void CanDb_EncodeAndPack(u8 *data, const can_sig_desc_t *sig, s32 value)
{
    const can_raw_t raw = CanDb_EncodeSignalValue(value, sig);
    CanDb_PackSignal(data, sig, raw);
}