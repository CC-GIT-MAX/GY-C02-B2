/**
 * @file    main.c
 * @brief   C02-B2 仪表 MCU 主入口
 */
#include "sdk_project_config.h"

#include "bsp_init.h"
#include "drv_init.h"
#include "can_if.h"
#include "power.h"
#include "rti.h"
#include "scheduler.h"

#define LOG_NAME  "MAIN"
#include "log.h"

void I2C_DRIVER_MASTER_CALLBACK(i2c_master_event_t event, void *userData)   { (void)event; (void)userData; }
void DMA_UART0_RX_FUNCTION_CALLBACK(void *parameter, dma_chn_status_t status){ (void)parameter; (void)status; }
void DMA_UART0_TX_FUNCTION_CALLBACK(void *parameter, dma_chn_status_t status){ (void)parameter; (void)status; }
void DMA_UART2_RX_FUNCTION_CALLBACK(void *parameter, dma_chn_status_t status){ (void)parameter; (void)status; }
void DMA_UART2_TX_FUNCTION_CALLBACK(void *parameter, dma_chn_status_t status){ (void)parameter; (void)status; }

int main(void)
{
    (void)BSP_Init();
    (void)DRV_Init();
    RTI_Init();
    (void)CanIf_Init();

    Scheduler_Init();
    if (Power_IsIgnOn()) {
        Scheduler_OnIgnOn();
    }
    LOG_I("=== C02-B2 boot OK ===");

    for (;;) {
        Scheduler_Run();
        __WFI();
    }
}
