/**
 * @file    wdg.c
 * @brief   Watchdog (WDG) wrapper implementation
 * @brief   看门狗封装实现
 */
#include "wdg.h"
#include "sdk_project_config.h"

#define MOD_NAME  "WDG"
#include "log.h"

c02b2_result_t Wdg_Init(void)
{
    if (WDG_DRV_Init(0, &wdg_config0) != STATUS_SUCCESS) {
        LOG_E("WDG_DRV_Init failed");
        return C02B2_ERR;
    }
    return C02B2_OK;
}

void Wdg_Trigger(void)
{
    WDG_DRV_Trigger(0);
}

u32 Wdg_GetCounter(void)
{
    return WDG_DRV_GetCounter(0);
}
