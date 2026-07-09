/**
 * @file    i2c.h
 * @brief   I2C peripheral driver API
 * @brief   I2C 外设驱动接口
 *
 * Single header that exposes both the module-level bring-up
 * (I2c_Init()) and any future common-use helpers.  This file
 * replaces the older app/drv_api/i2c/i2c_init.c + (missing .h)
 * layout.
 */
#ifndef C02B2_DRV_API_I2C_H
#define C02B2_DRV_API_I2C_H

#include "result.h"

/**
 * @brief   Initialize the I2C peripheral
 * @brief   初始化 I2C 外设
 *
 * @details Pulls the I2C configuration from board/i2c_config.c
 *          and applies it through the vendor SDK driver.
 *
 * @return  c02b2_result_t
 * @retval  C02B2_OK  Initialization succeeded
 */
c02b2_result_t I2c_Init(void);

#endif /* C02B2_DRV_API_I2C_H */
