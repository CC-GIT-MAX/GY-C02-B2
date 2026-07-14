/*
 * Copyright 2020-2026 Yuntu Microelectronics Co., Ltd.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * 
 * @file wdg_config.c
 * @brief 
 * 
 */



#include "wdg_config.h"


/*wdg_config0*/
const wdg_user_config_t wdg_config0 = {
    .clockSource=WDG_IPC_CLOCK,
    .opMode={
        .deepsleep=false,
        .debug=false,
    },
    .updateEnable=false,
    .intEnable=false,
    .winEnable=false,
    .windowValue=0,
    .timeoutValue=40000000,
    .apbErrorResetEnable=0,
};

