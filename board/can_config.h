/*
 * Copyright 2020-2026 Yuntu Microelectronics Co., Ltd.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * 
 * @file can_config.h
 * @brief 
 * 
 */




#ifndef __CAN_CONFIG_H__
#define __CAN_CONFIG_H__




#include "flexcan_driver.h"


#define public_can_INST 2
#define private_can_INST 1


extern flexcan_state_t public_can_State;
extern const  flexcan_user_config_t  public_can;


extern flexcan_state_t private_can_State;
extern const  flexcan_user_config_t  private_can;







#endif

