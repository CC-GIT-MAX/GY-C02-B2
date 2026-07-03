/**
 * @file    main.c
 * @brief   C02-B2 仪表 MCU 主入口
 *
 * 主循环采用"超级循环 + RTI 时间片调度"，由 app/scheduler 统一管理
 * 所有业务模块。本文件不包含任何业务逻辑。
 */
#include "sdk_project_config.h"

#include "bsp_init.h"
#include "drv_init.h"
#include "power.h"
#include "rti.h"
#include "scheduler.h"

#define LOG_NAME  "MAIN"
#include "log.h"

/* ISR 桩（厂商 SDK 模板留空回调） --------------------------------------- */
void I2C_DRIVER_MASTER_CALLBACK(i2c_master_event_t event, void *userData)
{
    (void)event; (void)userData;
}
void DMA_UART0_RX_FUNCTION_CALLBACK(void *parameter, dma_chn_status_t status) { (void)parameter; (void)status; }
void DMA_UART0_TX_FUNCTION_CALLBACK(void *parameter, dma_chn_status_t status) { (void)parameter; (void)status; }
void DMA_UART2_RX_FUNCTION_CALLBACK(void *parameter, dma_chn_status_t status) { (void)parameter; (void)status; }
void DMA_UART2_TX_FUNCTION_CALLBACK(void *parameter, dma_chn_status_t status) { (void)parameter; (void)status; }

/**
 * @brief Application entry point
 */
int main(void)
{
    (void)BSP_Init();
    (void)DRV_Init();
    RTI_Init();

    Scheduler_Init();

    /* If the IGN line is already high at boot (e.g. key-on before MCU ready),
     * fire the IGN ON lifecycle so all modules initialize their post-IGN state. */
    if (Power_IsIgnOn()) {
        Scheduler_OnIgnOn();
    }
    LOG_I("=== C02-B2 boot OK ===");

    /* Super loop: 调度器遍历所有已注册模块的 tick() */
    for (;;) {
        Scheduler_Run();
        __WFI();   /* 进入睡眠直到下一个 RTI 中断 */
    }
}
