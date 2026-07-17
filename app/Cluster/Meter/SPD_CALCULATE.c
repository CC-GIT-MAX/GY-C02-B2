/**
 * @file    SPD_CALCULATE.c
 * @brief   车速表计算层实现
 * @brief   Speedometer Calculation Layer Implementation
 *
 * 功能: 将车速值(SPD_INPUT_VALUE)转换为步进电机步数(SPD_NEW_STEPS)
 * 当前线性插值函数为空实现 (SPD_CALC_LINEAR直接返回0)
 */
#include "SPD_CALCULATE.h"
#include "SPD_INPUT.h"
#include "SPD_OUTPUT.h"

#define MOD_NAME  "SPC"
#include "log.h"

/* 模块变量 */
u16 SPD_NOW_STEPS, SPD_NEW_STEPS, SPD_TAR_STEPS;

/**
 * @brief   MCU复位初始化
 */
void SPD_CALC_INIT_RESET(u8 cold_boot)
{
    if (cold_boot) { SPD_CALC_INIT_IGN(); SPD_NOW_STEPS = 0; }
}

/**
 * @brief   IGN ON初始化
 */
void SPD_CALC_INIT_IGN(void)
{
    SPD_NEW_STEPS = 0;
    SPD_TAR_STEPS = 0;
}

/**
 * @brief   待机处理
 */
void SPD_CALC_STANDBY(void)
{
    SPD_NEW_STEPS = 0;
    SPD_TAR_STEPS = 0;
}

/**
 * @brief   车速计算主函数 - 20ms周期调用
 * @brief   Main speed calculation - Called every 20ms
 *
 * 流程: SPD_INPUT_VALUE → 线性插值 → SPD_NEW_STEPS
 */
void SPD_CALCULATE(void)
{
    SPD_NEW_STEPS = SPD_CALC_LINEAR(SPD_INPUT_VALUE);
}

/**
 * @brief   设置步进电机移动速度 (当前为空实现)
 */
void SPD_CALC_SET_MOVE_SPD(void)
{
}

/**
 * @brief   线性插值 (车速→步数)
 * @brief   Linear interpolation (speed → stepper steps)
 *
 * @param   in_value  车速 (0.1km/h)
 * @return  步进电机步数
 *
 * 当前为空实现, 需要根据实际V-R曲线配置SPD_IN_TAB/SPD_OUT_TAB
 */
u16 SPD_CALC_LINEAR(u16 in_value)
{
    (void)in_value;
    return 0;
}
