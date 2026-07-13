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

void Io_WritePins(void *base, u32 pin);

void Io_WritePin(void *base, u32 pin, pins_level_type_t level);

u32 Io_ReadPins(void *base, u32 pin);

bool Io_ReadPin(void *base, u32 pin);

void Io_TogglePin(void *base, u32 pin);

void Io_SetMuxModeSel(void *base, u32 pin, u32 mode);

void Io_SetPinDirection(void *base, u32 pin, pin_direction_t direction);

u32 Io_GetPinsDirection(void *base, u32 pin);

u32 Io_GetPortIntFlags(void *base);
