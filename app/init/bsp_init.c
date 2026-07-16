/**
 * @file    bsp_init.c
 * @brief   BSP init: clock, pin mux, DMA, WDG, power manager
 */
#include "bsp_init.h"

#include "sdk_project_config.h"

#define MOD_NAME  "BSP"
#include "log.h"

/**
 * @brief   Bring up the board support package
 * @brief   初始化 BSP
 *
 * @details 初始化顺序重要：
 *   1. CLOCK 首先——其他外设都依赖于系统时钟
 *   2. UpdateConfiguration ——初始化后应用时钟树变更
 *   3. PINS ——所有引脚复用必须在任何驱动接触引脚之前设置
 *   4. DMA ——控制器必须在外设使用 DMA 之前就绪
 *   5. POWER_SYS ——配置低功耗模式
 *   6. WDG ——最后启用看门狗，避免初始化流程被中断
 *   7. INT_SYS ——硬件就绪后安装中断处理
 *
 * @return  c02b2_result_t    C02B2_OK: Initialization succeeded (always; vendor errors are logged only)
 */
c02b2_result_t BSP_Init(void)
{
    /* 时钟树：必须在任何外设驱动之前运行。 */
    CLOCK_SYS_Init(g_clockManConfigsArr,
                   CLOCK_MANAGER_CONFIG_CNT,
                   g_clockManCallbacksArr,
                   CLOCK_MANAGER_CALLBACK_CNT);
    /* 应用所选时钟策略（如 RUN/HSRUN）。 */
    CLOCK_SYS_UpdateConfiguration(CLOCK_MANAGER_ACTIVE_INDEX,
                                  CLOCK_MANAGER_POLICY_AGREEMENT);

    /* 引脚复用：在任何外设接触引脚之前配置所有 pad。 */
    PINS_DRV_Init(NUM_OF_CONFIGURED_PINS0, g_pin_mux_InitConfigArr0);
    /* DMA 控制器：必须在外设使用 DMA 之前就绪。 */
    DMA_DRV_Init(&dmaState,
                 &dmaController_InitConfig,
                 dmaChnState,
                 dmaChnConfigArray,
                 NUM_OF_CONFIGURED_DMA_CHANNEL);
    /* 电源管理器：配置低功耗模式（RUN/STOP 等）。 */
    POWER_SYS_Init(&powerConfigsArr,
                   POWER_MANAGER_CONFIG_CNT,
                   NULL,
                   POWER_MANAGER_CALLBACK_CNT);
    /* 看门狗：最后启用，避免初始化序列被中断。 */
    WDG_DRV_Init(0, &wdg_config0);
    /* 安装默认异常/中断处理程序。 */
    INT_SYS_ConfigInit();

    LOG_I("BSP init OK");
    return C02B2_OK;
}
