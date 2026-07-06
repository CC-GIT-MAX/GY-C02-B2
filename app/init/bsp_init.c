/**
 * @file    bsp_init.c
 * @brief   BSP init: clock, pin mux, DMA, WDG, power manager
 */
#include "bsp_init.h"

#include "sdk_project_config.h"

#define LOG_NAME  "BSP"
#include "log.h"

/**
 * @brief   Bring up the board support package
 * @brief   初始化 BSP
 *
 * @details Init order matters:
 *   1. CLOCK first - all other peripherals depend on the system clock
 *   2. UpdateConfiguration - apply post-init clock tree changes
 *   3. PINS - GPIO mux must be set before any driver that touches pads
 *   4. DMA - controllers must be ready before peripheral DMA usage
 *   5. POWER_SYS - configure low-power modes
 *   6. WDG - enable the watchdog last so other inits cannot be interrupted
 *   7. INT_SYS - install ISRs once hardware is ready
 *
 * @return  c02b2_result_t
 * @retval  C02B2_OK  Initialization succeeded (always; vendor errors are logged only)
 */
c02b2_result_t BSP_Init(void)
{
    /* System clock tree: must run before any peripheral driver. */
    CLOCK_SYS_Init(g_clockManConfigsArr,
                   CLOCK_MANAGER_CONFIG_CNT,
                   g_clockManCallbacksArr,
                   CLOCK_MANAGER_CALLBACK_CNT);
    /* Apply the chosen clock policy (e.g. RUN/HSRUN). */
    CLOCK_SYS_UpdateConfiguration(CLOCK_MANAGER_ACTIVE_INDEX,
                                  CLOCK_MANAGER_POLICY_AGREEMENT);

    /* Pin mux: configure all pads before any peripheral touches them. */
    PINS_DRV_Init(NUM_OF_CONFIGURED_PINS0, g_pin_mux_InitConfigArr0);
    /* DMA controller: must be ready before peripherals that use DMA. */
    DMA_DRV_Init(&dmaState,
                 &dmaController_InitConfig,
                 dmaChnState,
                 dmaChnConfigArray,
                 NUM_OF_CONFIGURED_DMA_CHANNEL);
    /* Power manager: configure low-power modes (RUN/STOP/etc.). */
    POWER_SYS_Init(&powerConfigsArr,
                   POWER_MANAGER_CONFIG_CNT,
                   NULL,
                   POWER_MANAGER_CALLBACK_CNT);
    /* Watchdog: enable LAST so init sequence isn't interrupted. */
    WDG_DRV_Init(0, &wdg_config0);
    /* Install default exception/interrupt handlers. */
    INT_SYS_ConfigInit();

    LOG_I("BSP init OK");
    return C02B2_OK;
}
