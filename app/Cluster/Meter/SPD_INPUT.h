/**
 * @file    SPD_INPUT.h
 * @brief   车速表输入层 - CAN车速信号处理、显示值计算
 * @brief   Speedometer Input Layer - CAN vehicle speed processing, display value calculation
 *
 * 信号输入 (Signal Inputs):
 *   - 0x125 ESC_VehicleSpeed: 车速 (原始值, 0.05625 km/h/bit)
 *   - 0x125 ESC_VehicleSpeedInvalid: 车速有效标志
 *
 * 信号输出 (Internal Variables):
 *   - SPD_INPUT_VALUE: 车速 (单位: 0.1km/h)
 *   - SPD_DISP_VALUE: 显示车速 (单位: km/h)
 *   - SPD_DISP_CAN_VALUE: CAN外发车速值
 *   - SPD_VEHICLE_RUNING: 车辆行驶标志 (>2.5km/h=1, <2.0km/h=0)
 */
#ifndef C02B2_SPD_INPUT_H
#define C02B2_SPD_INPUT_H

#include "types.h"

/* 公共函数 */
void SPD_INPUT_INIT_RESET(u8 cold_boot);
void SPD_INPUT_INIT_IGN(void);
void SPD_INPUT_STANDBY(void);
void SPD_INPUT(void);

/* 宏定义 */
#define mSPD_Over2km    (SPD_INPUT_VALUE>=20)

/* 配置常量 (来自METER.h) */
#define SPD_INPUT_QUEUE_LENGTH    8
#define SPD_PULSE_MODE            0
#define SPD_CAN_MODE              1
#define SPD_RATE_ROM              3848

/* 公共变量 */
extern u16 SPD_INPUT_VALUE;         /**< 车速 (0.1km/h) */
extern u8 SPD_INPUT_MODE;           /**< 输入模式 (CAN/PULSE) */
extern u16 SPD_INPUT_QUEUE[];       /**< 滤波队列 */

extern u16 SPD_IN_TAB[16];
extern u16 SPD_OUT_TAB[16];


extern u8 SPD_NO_SIGNAL_COUNTER;

extern u16 SPD_DISP_VALUE;          /**< 显示车速 (km/h) */
extern u16 SPD_DISP_CAN_VALUE;      /**< CAN外发车速值 */

extern u8 SPD_VEHICLE_RUNING;       /**< 车辆行驶标志 */

#endif
