/**
 * @file    SPD_INPUT.c
 * @brief   车速表输入层实现 - CAN车速信号处理
 * @brief   Speedometer Input Layer Implementation
 *
 * 功能规范: VX11-A5功能规范v1.8 章节3.1 车速表
 *
 * CAN信号:
 *   0x125 ESC_VehicleSpeed: 车速 (0.05625 km/h/bit)
 *   0x125 ESC_VehicleSpeedInvalid: 0=有效, 1=无效
 *
 * 车速计算:
 *   SPD_INPUT_VALUE = ESC_VehicleSpeed * 5625 / 10000 (单位: 0.1km/h)
 *   最大值: 4606 (460.6 km/h)
 *
 * 行驶标志:
 *   >2.5km/h: SPD_VEHICLE_RUNING=1
 *   <2.0km/h: SPD_VEHICLE_RUNING=0
 */
#include "SPD_INPUT.h"
#include "signal.h"
#include "POWER_MODE.h"

#define MOD_NAME  "SPI"
#include "log.h"



/* 模块变量 */
u16 SPD_PULSE_WIDTH;
u16 SPD_INPUT_VALUE;               /**< 车速 (0.1km/h) */
u16 SPD_INPUT_QUEUE[SPD_INPUT_QUEUE_LENGTH];

u8 SPD_INPUT_MODE;

u8 SPD_NO_SIGNAL_COUNTER;


u16 SPD_DISP_VALUE;                /**< 显示车速 (km/h) */
u16 SPD_DISP_CAN_VALUE;            /**< CAN外发车速值 */

u8 SPD_VEHICLE_RUNING;             /**< 车辆行驶标志 */
u8 ESC_VehicleSpeedInvalid_2s;     /**< 车速无效2s计时 */

/**
 * @brief   MCU复位初始化
 */
void SPD_INPUT_INIT_RESET(u8 cold_boot)
{
    if (cold_boot) SPD_INPUT_INIT_IGN();
}

/**
 * @brief   IGN ON初始化
 */
void SPD_INPUT_INIT_IGN(void)
{
    u8 i;
    for (i = 0; i < SPD_INPUT_QUEUE_LENGTH; i++) SPD_INPUT_QUEUE[i] = 0xFFFF;
    SPD_INPUT_VALUE = 0;
    SPD_INPUT_MODE = SPD_CAN_MODE;
    SPD_NO_SIGNAL_COUNTER = 0;
    SPD_DISP_VALUE = 0;
    SPD_DISP_CAN_VALUE = 0;
    SPD_VEHICLE_RUNING = 0;
    ESC_VehicleSpeedInvalid_2s = 0;
}

/**
 * @brief   待机处理
 */
void SPD_INPUT_STANDBY(void)
{
    SPD_INPUT_VALUE = 0;
    SPD_DISP_VALUE = 0;
    SPD_VEHICLE_RUNING = 0;
    SPD_DISP_CAN_VALUE = 0;
}

/**
 * @brief   车速输入主处理 - 20ms周期调用
 * @brief   Main speed input processing - Called every 20ms
 *
 * 流程:
 *   1. 从CAN信号获取车速 (Signal_Get)
 *   2. 计算行驶标志
 *   3. 计算显示车速值
 */
void SPD_INPUT(void)
{
    u32 temp;
    u16 raw_speed;
    u8 speed_invalid;
    u8 speed_valid;

    /* 从C02信号总线获取CAN数据 */
    raw_speed = (u16)Signal_Get(SIG_CAN_ESC_VehicleSpeed);
    speed_invalid = (u8)Signal_Get(SIG_CAN_ESC_VehicleSpeedInvalid);
    speed_valid = Signal_IsValid(SIG_CAN_ESC_VehicleSpeed);

    /* IGN ON后1s内车速为0 */
    if (POWER_IGN_COUNTER < 1) SPD_INPUT_VALUE = 0;
    else if (SPD_INPUT_MODE == SPD_CAN_MODE)
    {
        /* CAN模式: ESC_VehicleSpeed * 0.05625 km/h → * 5625 / 100000 → 0.1km/h */
        if (speed_valid && !speed_invalid && (raw_speed < 0x1FFE))
        {
            temp = raw_speed;
            temp *= 5625;           /* 0.005625 km/h → 放大100000倍 */
            if ((temp % 10000) >= 5000) { temp = temp / 10000; temp += 1; }  /* 四舍五入 */
            else temp /= 10000;
            if (temp > 4606) temp = 4606;   /* 上限460.6 km/h */
        }
        else temp = 0;
        SPD_INPUT_VALUE = (u16)temp;
    }
    else SPD_INPUT_MODE = SPD_CAN_MODE;     /* 默认CAN模式 */

    /* 行驶标志判断 */
    if (!speed_valid) SPD_VEHICLE_RUNING = 0;
    else if (speed_invalid || (raw_speed >= 0x1FFE)) SPD_VEHICLE_RUNING = 0;
    else if (raw_speed > 44) SPD_VEHICLE_RUNING = 1;    /* >2.5 km/h */
    else if (raw_speed < 36) SPD_VEHICLE_RUNING = 0;    /* <2.0 km/h */

    /* CAN外发车速值 (IPK_vDisplay) */
    if (!speed_valid) { SPD_DISP_CAN_VALUE = 0x0; ESC_VehicleSpeedInvalid_2s = 0; }
    else if (raw_speed >= 0x1FFE) { SPD_DISP_CAN_VALUE = 0x1FFF; ESC_VehicleSpeedInvalid_2s = 0; }
    else if (speed_invalid)
    {
        /* 车速无效: 2s后显示--- */
        if (ESC_VehicleSpeedInvalid_2s < 200) ESC_VehicleSpeedInvalid_2s++;
        else SPD_DISP_CAN_VALUE = 0x1FFF;
    }
    else
    {
        ESC_VehicleSpeedInvalid_2s = 0;
        if (raw_speed < 10) temp = 0;
        else
        {
            temp = raw_speed;
            temp *= 103;                       /* *1.03 校准 */
            if (raw_speed > 355) temp += 2222; /* +1.25 */
            if (raw_speed > 355) temp /= 100;
            else temp = temp / 100 + 1;
            if (raw_speed >= 0x1FFE) temp = 0x1FFE;
            else if (temp >= 0x10AB) temp = 0x10AB;  /* 外发显示上限 */
        }
        SPD_DISP_CAN_VALUE = (u16)temp;
    }

    /* 显示车速值 (km/h) */
    if (!speed_valid) SPD_DISP_VALUE = 0;
    else if (speed_invalid || (raw_speed >= 0x1FFE)) SPD_DISP_VALUE = 0;
    else if (raw_speed < 10) SPD_DISP_VALUE = 0;
    else
    {
        temp = SPD_DISP_CAN_VALUE;
        temp *= 5625;
        if (raw_speed > 355)
        {
            temp /= 100000;        /* 向下取整 */
        }
        else
        {
            if (temp % 100000) temp = (temp / 100000) + 1;  /* 向上取整 */
            else temp /= 100000;
        }
        SPD_DISP_VALUE = (u16)(temp);
    }
}
