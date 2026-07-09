/**
 * @file    io.h
 * @brief   GPIO / pin wrapper API
 * @brief   GPIO / 引脚封装接口
 *
 * Thin wrappers around the vendor pins_driver so business modules
 * do not include the SDK headers directly.  Common-use helpers
 * (set output, read input, configure pin mux) land here.
 *
 * Note: PINS_DRV_Init() is intentionally NOT exposed -- the
 * pin-mux + GPIO init is performed once during DRV_Init() from
 * board/pin_mux.c.  Calling it twice would re-program every pin
 * and clobber any in-flight peripheral configuration.
 */
#ifndef C02B2_DRV_API_IO_H
#define C02B2_DRV_API_IO_H

#include "types.h"

/**
 * @brief   Write a single GPIO pin high or low
 * @brief   设置单个 GPIO 引脚电平
 *
 * @param[in]  base   GPIO_Type base (e.g. PTA, PTB, ...)
 * @param[in]  pin    pin mask (single bit, e.g. (1u << 5))
 * @param[in]  level  0 = low, non-zero = high
 */
void Io_WritePin(void *base, u32 pin, u32 level);

/**
 * @brief   Read a single GPIO pin level
 * @brief   读取单个 GPIO 引脚电平
 *
 * @param[in]  base  GPIO_Type base
 * @param[in]  pin   pin mask (single bit)
 *
 * @return  u32  0 = low, non-zero = high
 */
u32 Io_ReadPin(void *base, u32 pin);

/**
 * @brief   Toggle a single GPIO pin
 * @brief   翻转单个 GPIO 引脚电平
 *
 * @param[in]  base  GPIO_Type base
 * @param[in]  pin   pin mask (single bit)
 */
void Io_TogglePin(void *base, u32 pin);

#endif /* C02B2_DRV_API_IO_H */
