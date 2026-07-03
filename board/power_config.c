/*
 * Copyright 2020-2026 Yuntu Microelectronics Co., Ltd.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * 
 * @file power_config.c
 * @brief 
 * 
 */


#include <stddef.h>
#include "power_config.h"




/* pwrMan_InitConfigPowerDown */
power_manager_user_config_t pwrMan_InitConfigPowerDown={
    .powerMode=POWER_MANAGER_STANDBY,
    .sleepOnExitValue=false,
};





power_manager_user_config_t* powerConfigsArr[1]={
    &pwrMan_InitConfigPowerDown,
};


