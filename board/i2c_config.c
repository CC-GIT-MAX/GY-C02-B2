/*
 * Copyright 2020-2026 Yuntu Microelectronics Co., Ltd.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * 
 * @file i2c_config.c
 * @brief 
 * 
 */


#include <stddef.h>
#include "i2c_config.h"

/* I2C master */
/*I2C_MasterConfig0*/
extern void I2C_DRIVER_MASTER_CALLBACK(i2c_master_event_t event, void *userData);


i2c_master_state_t I2C_MasterConfig0_State;
const i2c_master_user_config_t I2C_MasterConfig0 = {
    .slaveAddress=0x50U,
    .is10bitAddr=false,
    .operatingMode=I2C_STANDARD_MODE,
    .transferType=I2C_USING_INTERRUPTS,
    .baudRate=100000,
    .dmaChannel=0,
    .masterCallback=I2C_DRIVER_MASTER_CALLBACK,
    .callbackParam=NULL,
};
/* I2C slave */

