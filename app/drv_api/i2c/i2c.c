/**
 * @file    i2c.c
 * @brief   I2C peripheral driver implementation
 * @brief   I2C 外设驱动实现
 *
 * Module-level bring-up + (future) common-use helpers live here.
 * Replaces the older app/drv_api/i2c/i2c_init.c.
 */
#include "i2c.h"
#include "sdk_project_config.h"

#define LOG_NAME  "I2C"
#include "log.h"

c02b2_result_t I2c_Init(void)
{
    I2C_DRV_MasterInit(0, &I2C_MasterConfig0, &I2C_MasterConfig0_State);
    return C02B2_OK;
}
