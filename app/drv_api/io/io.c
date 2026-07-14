/**
 * @file    io.c
 * @brief   GPIO / pin wrapper implementation
 * @brief   GPIO / 引脚封装实现
 */
#include "io.h"
#include "pins_driver.h"
/* REVIEW: Baseline 1 违例：drv_api 直接包含 pins_driver.h（Phase 3 拆分 IO 抽象） */


void Io_WritePins(void *base, u32 pin)
{
    PINS_DRV_WritePins((GPIO_Type *)base, pin);
}

void Io_WritePin(void *base, u32 pin, pins_level_type_t level)
{
    PINS_DRV_WritePin((GPIO_Type *)base, pin, level);
}

u32 Io_ReadPins(void *base, u32 pin)
{
    return (u32)((PINS_DRV_ReadPins((GPIO_Type *)base) & pin) ? 1u : 0u);
}

bool Io_ReadPin(void *base, u32 pin)
{
    return (bool)(PINS_DRV_ReadPin((GPIO_Type *)base, pin));
}

void Io_TogglePin(void *base, u32 pin)
{
    const u32 cur = Io_ReadPin(base, pin);
    Io_WritePin(base, pin, cur ^ 1u);
}

void Io_SetMuxModeSel(void *base, u32 pin, u32 mode)
{
    PINS_DRV_SetMuxModeSel((PCTRL_Type *)base, pin, (port_mux_t)mode);
}

void Io_SetPinDirection(void *base, u32 pin, pin_direction_t direction)
{
    PINS_DRV_SetPinDirection((GPIO_Type *)base, pin, direction);
}

u32 Io_GetPinsDirection(void *base, u32 pin)   
{
    return PINS_DRV_GetPinsDirection((GPIO_Type *)base, pin);
}

u32 Io_GetPortIntFlags(void *base)
{
    return PINS_DRV_GetPortIntFlags((GPIO_Type *)base);
}



