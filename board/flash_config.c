/*
 * Copyright 2020-2026 Yuntu Microelectronics Co., Ltd.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * 
 * @file flash_config.c
 * @brief 
 * 
 */


#include <stddef.h>
#include "flash_config.h"


/*flash_config0*/

flash_state_t flash_config0_State;



flash_user_config_t flash_config0={
    .async=false,
    .disGlobalInt=true,
    .readVerify=false,
    .callback=NULL,
};

