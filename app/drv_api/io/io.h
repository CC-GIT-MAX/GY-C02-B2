/**
 * @file    io.h
 * @brief   GPIO / pin wrapper API
 * @brief   GPIO / 引脚封装接口
 *
 * 对厂商 pins_driver 的薄封装，让业务模块无需直接包含 SDK
 * 头文件。通用辅助函数（设置输出、读取输入、配置 pin mux）
 * 均放在本头文件中。
 *
 * 注：PINS_DRV_Init() 故意不导出 —— pin-mux + GPIO 初始化
 * 仅由 board/pin_mux.c 在 DRV_Init() 中执行一次。重复调用会
 * 重新配置所有引脚，破坏进行中的外设配置。
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
