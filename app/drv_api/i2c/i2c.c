/**
 * @file    i2c.c
 * @brief   I2C peripheral driver implementation
 * @brief   I2C 外设驱动实现
 *
 * 模块级初始化以及（未来可能新增的）公共辅助函数均在本文件实现。
 * 取代了较早的 app/drv_api/i2c/i2c_init.c。
 */
#include "i2c.h"
#include "sdk_project_config.h"

#define MOD_NAME  "I2C"
#include "log.h"

c02b2_result_t I2c_Init(void)
{
    I2C_DRV_MasterInit(0, &I2C_MasterConfig0, &I2C_MasterConfig0_State);
    return C02B2_OK;
}
