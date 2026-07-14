/**
 * @file    types.h
 * @brief   Project-wide standard type aliases
 *
 * All C99 fixed-width integer types come from <stdint.h>.
 * We only alias them so that business code does not need to remember
 * `<stdint.h>` includes everywhere, and so that we can swap definitions
 * (e.g. for unit-test builds) in one place.
 *
 * Do NOT redefine the underlying types here; rely on the standard headers.
 */
#ifndef C02B2_TYPES_H
#define C02B2_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Fixed-width signed */
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef int64_t  s64;

/* Fixed-width unsigned */
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

/* Legacy-compatible aliases (for migration of old code).
 * Prefer the short forms in new code. */
typedef uint8_t  uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef int8_t   int8;
typedef int16_t  int16;
typedef int32_t  int32;

typedef uint8_t  byte;
typedef uint16_t word;
typedef uint32_t dword;

#endif /* C02B2_TYPES_H */
