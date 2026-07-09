/**
 * @file    i2c_init.c
 * @brief   Initialize I2C instance 0 (master mode)
 */
#include "sdk_project_config.h"

/**
 * @brief   Initialize I2C instance 0 as master
 *
 * @details The cluster uses I2C to talk to the touch controller and
 *          any IO expanders wired on the same bus. Pulls the config
 *          from board/i2c_config.c.
 */
void I2c_Init(void)
{
    I2C_DRV_MasterInit(0, &I2C_MasterConfig0, &I2C_MasterConfig0_State);
}
