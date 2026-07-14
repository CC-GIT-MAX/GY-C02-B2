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
 * @brief   Write a masked set of pins on a GPIO port
 * @brief   按位掩码写入 GPIO 端口引脚
 *
 * @param[in]  base  GPIO_Type 端口基址（如 PTA / PTB）
 * @param[in]  pin   1 bit = 1 pin 的位掩码
 */
void Io_WritePins(void *base, u32 pin);

/**
 * @brief   Write a single pin to high/low level
 * @brief   设置单个引脚输出电平
 *
 * @param[in]  base   GPIO_Type 端口基址
 * @param[in]  pin    单个引脚的位掩码（仅最低 1 位有效）
 * @param[in]  level  输出电平（PINS_LEVEL_HIGH / PINS_LEVEL_LOW）
 */
void Io_WritePin(void *base, u32 pin, pins_level_type_t level);

/**
 * @brief   Test whether any requested pin on a GPIO port is set high
 * @brief   读取端口并判断指定引脚集合中是否任一为高
 *
 * @param[in]  base  GPIO_Type 端口基址
 * @param[in]  pin   待检测的引脚位掩码
 *
 * @return  u32  1 = 掩码中至少一位为高；0 = 全部为低
 */
u32 Io_ReadPins(void *base, u32 pin);

/**
 * @brief   Read a single pin logic level
 * @brief   读取单个引脚电平
 *
 * @param[in]  base  GPIO_Type 端口基址
 * @param[in]  pin   单个引脚的位掩码
 *
 * @return  bool  true = 高电平；false = 低电平
 */
bool Io_ReadPin(void *base, u32 pin);

/**
 * @brief   Toggle a single pin by reading then XORing with 1
 * @brief   翻转单个引脚输出电平
 *
 * @param[in]  base  GPIO_Type 端口基址
 * @param[in]  pin   单个引脚的位掩码
 */
void Io_TogglePin(void *base, u32 pin);

/**
 * @brief   Configure a pin alternate-function mux
 * @brief   配置引脚的复用功能（pin mux）
 *
 * @param[in]  base  PCTRL_Type 端口控制基址
 * @param[in]  pin   单个引脚的位掩码
 * @param[in]  mode  port_mux_t 复用选项
 */
void Io_SetMuxModeSel(void *base, u32 pin, u32 mode);

/**
 * @brief   Configure a pin GPIO direction (input or output)
 * @brief   配置单个引脚的 GPIO 方向（输入 / 输出）
 *
 * @param[in]  base       GPIO_Type 端口基址
 * @param[in]  pin        单个引脚的位掩码
 * @param[in]  direction  PIN_INPUT / PIN_OUTPUT
 */
void Io_SetPinDirection(void *base, u32 pin, pin_direction_t direction);

/**
 * @brief   Read direction of a single pin on a GPIO port
 * @brief   读取端口中单个引脚的方向
 *
 * @param[in]  base  GPIO_Type 端口基址
 * @param[in]  pin   单个引脚的位掩码
 *
 * @return  u32  1 = 输入；0 = 输出
 */
u32 Io_GetPinsDirection(void *base, u32 pin);

/**
 * @brief   Read the entire GPIO port interrupt status flag register
 * @brief   读取 GPIO 端口的中断状态标志寄存器
 *
 * @param[in]  base  GPIO_Type 端口基址
 *
 * @return  u32  端口中断标志位图
 */
u32 Io_GetPortIntFlags(void *base);

#endif /* C02B2_DRV_API_IO_H */
