/**
 * @file    i2c.h
 * @brief   I2C peripheral driver API
 * @brief   I2C 外设驱动接口
 *
 * 该单头文件同时暴露模块级初始化（I2c_Init()）
 * 与未来可能新增的公共辅助函数。本文件取代了较早的
 * app/drv_api/i2c/i2c_init.c + 缺失 .h 的布局。
 */
#ifndef C02B2_DRV_API_I2C_H
#define C02B2_DRV_API_I2C_H

#include "result.h"

/**
 * @brief   Initialize the I2C peripheral
 * @brief   初始化 I2C 外设
 *
 * @details 从 board/i2c_config.c 取出 I2C 配置，
 *          并通过厂商 SDK 驱动应用之。
 *
 * @return  c02b2_result_t
 * @retval  C02B2_OK  Initialization succeeded
 */
c02b2_result_t I2c_Init(void);

#endif /* C02B2_DRV_API_I2C_H */
