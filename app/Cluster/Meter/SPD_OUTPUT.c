/**
 * @file    SPD_OUTPUT.c
 * @brief   车速表输出层实现 - 步进电机驱动
 * @brief   Speedometer Output Layer Implementation
 *
 * 功能: 控制步进电机跟随目标位置移动
 * 当前频率输出功能为空实现 (需要硬件PWM支持)
 */
#include "SPD_OUTPUT.h"
#include "SPD_CALCULATE.h"
#include "SPD_INPUT.h"

#define MOD_NAME  "SPO"
#include "log.h"

/**
 * @brief   MCU复位初始化
 */
void SPD_OUTPUT_INIT_RESET(u8 cold_boot)
{
    if (cold_boot) { }
}

/**
 * @brief   IGN ON初始化
 */
void SPD_OUTPUT_INIT_IGN(void)
{
}

/**
 * @brief   待机处理
 */
void SPD_OUTPUT_STANDBY(void)
{
}

/**
 * @brief   设置车速脉冲输出频率 (当前为空实现)
 * @brief   Set speed pulse output frequency (currently stub)
 *
 * 原始实现: 根据SPD_INPUT_VALUE计算PWM频率
 *   freq = 625000 / SPD_INPUT_VALUE
 */
void SPD_OUTPUT_SET_FREQ(void)
{
}

/**
 * @brief   步进电机移动一步
 * @brief   Move stepper motor one step
 *
 * 将SPD_NOW_STEPS逐步逼近SPD_TAR_STEPS
 * 每次调用移动1步
 */
void SPD_MOVE(void)
{
    if (SPD_NOW_STEPS < SPD_TAR_STEPS)
    {
        SPD_NOW_STEPS++;
    }
    else if (SPD_NOW_STEPS > SPD_TAR_STEPS)
    {
        SPD_NOW_STEPS--;
    }
}
