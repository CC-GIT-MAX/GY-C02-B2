/**
 * @file    io.c
 * @brief   GPIO / pin wrapper implementation
 * @brief   GPIO / 引脚封装实现
 */
#include "io.h"
#include "pins_driver.h"
/* REVIEW: Baseline 1 违例：drv_api 直接包含 pins_driver.h（Phase 3 拆分 IO 抽象） */

void Io_WritePin(void *base, u32 pin, u32 level)
{
    if (level != 0u) {
        PINS_DRV_WritePin((GPIO_Type *)base, pin, 1u);
    } else {
        PINS_DRV_WritePin((GPIO_Type *)base, pin, 0u);
    }
}

u32 Io_ReadPin(void *base, u32 pin)
{
    return (u32)((PINS_DRV_ReadPins((GPIO_Type *)base) & pin) ? 1u : 0u);
}

void Io_TogglePin(void *base, u32 pin)
{
    const u32 cur = Io_ReadPin(base, pin);
    Io_WritePin(base, pin, cur ^ 1u);
}
