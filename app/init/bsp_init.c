/**
 * @file    bsp_init.c
 * @brief   BSP init: clock, pin mux, DMA, WDG, power manager
 */
#include "bsp_init.h"

#include "sdk_project_config.h"

#define LOG_NAME  "BSP"
#include "log.h"

lbx_result_t BSP_Init(void)
{
    CLOCK_SYS_Init(g_clockManConfigsArr,
                   CLOCK_MANAGER_CONFIG_CNT,
                   g_clockManCallbacksArr,
                   CLOCK_MANAGER_CALLBACK_CNT);
    CLOCK_SYS_UpdateConfiguration(CLOCK_MANAGER_ACTIVE_INDEX,
                                  CLOCK_MANAGER_POLICY_AGREEMENT);

    PINS_DRV_Init(NUM_OF_CONFIGURED_PINS0, g_pin_mux_InitConfigArr0);
    DMA_DRV_Init(&dmaState,
                 &dmaController_InitConfig,
                 dmaChnState,
                 dmaChnConfigArray,
                 NUM_OF_CONFIGURED_DMA_CHANNEL);
    POWER_SYS_Init(&powerConfigsArr,
                   POWER_MANAGER_CONFIG_CNT,
                   NULL,
                   POWER_MANAGER_CALLBACK_CNT);
    WDG_DRV_Init(0, &wdg_config0);
    INT_SYS_ConfigInit();

    LOG_I("BSP init OK");
    return LBX_OK;
}
