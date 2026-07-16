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


typedef struct
{
    uint16   bit0: 1;
    uint16   bit1: 1;
    uint16   bit2: 1;
    uint16 bit3: 1;
    uint16 bit4: 1;
    uint16 bit5: 1;
    uint16 bit6: 1;
    uint16 bit7: 1;
    uint16 bit8: 1;
    uint16 bit9: 1;
    uint16 bit10: 1;
    uint16 bit11: 1;
    uint16 bit12: 1;
    uint16 bit13: 1;
    uint16 bit14: 1;
    uint16 bit15: 1;
} T_BITFLD16;


/* Word with msb and lsb handling */
typedef union
{
    struct
    {
        uint8  lsb;		 /*********************** intel format here */
        uint8  msb;
    } by;
    uint16  wo;
} T_BYTEFLD;

/* Carrier of 16 bits with word or byte (msb & lsb) carrier handling */
typedef union
{
    T_BITFLD16	  bi;
    T_BYTEFLD 	  cr;
} T_FLAG16;


typedef struct
{
    unsigned int	bit0: 1;
    unsigned int	bit1: 1;
    unsigned int	bit2: 1;
    unsigned int	bit3: 1;
    unsigned int	bit4: 1;
    unsigned int	bit5: 1;
    unsigned int	bit6: 1;
    unsigned int	bit7: 1;
} T_BITFLD8;
/* Carrier of 8 bits with byte */
typedef union
{
    T_BITFLD8	bi;
    uint8 by;
} T_FLAG8;
#endif /* C02B2_TYPES_H */
