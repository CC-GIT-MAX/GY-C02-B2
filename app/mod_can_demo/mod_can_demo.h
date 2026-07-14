/**
 * @file    mod_can_demo.h
 * @brief   Demo / bring-up module that exercises the full CAN stack
 * @brief   演练完整 CAN 栈的 demo / 联调模块
 *
 * @details 每秒一个周期，端到端跑遍所有层：
          - 信号总线读    (在几个 IPK 信号上调用 Signal_Get)
          - 超时位图      (将 SIG_CAN_RX_TIMEOUT_MAP 的位反向解码为 CAN id)
          - 原始帧缓存    (对指定 ID 调用 CanRx_GetLastRawFrame)
          - 整帧 TX      (CanTx_PreparePayload + CanTx_Trigger)
          - 单信号 TX    (CanTx_EncodeSignal + CanTx_Trigger)
          - raw 与物理量互转 (CanDb_PackSignal / DecodeSignal /
                               EncodeSignalValue，作用于 EMS_EngineSpeedRPM
                               和 ESC_VehicleSpeed)
 *
 * TX id 仅取自 IPK DBC (CAN_DB_IPK_TX_COUNT 项)；
 * raw 读是 IPK 的一个 RX id。运行此 demo 不需要导入完整 DBC。
 *
 * 编译期开关（详见 mod_can_demo.c）：
 *   MOD_CAN_DEMO_EN = 0（默认）——模块描述符已注册，tick 为 no-op。
 *   MOD_CAN_DEMO_EN = 1         ——六个 demo 每秒全部触发。 */
#ifndef C02B2_MOD_CAN_DEMO_H
#define C02B2_MOD_CAN_DEMO_H

#include "scheduler.h"

/**
 * @brief   Module descriptor for mod_can_demo (registered in scheduler.c)
 * @brief   mod_can_demo 的模块描述符（在 scheduler.c 中注册）
 */
extern const mod_desc_t mod_can_demo;

#endif /* C02B2_MOD_CAN_DEMO_H */
