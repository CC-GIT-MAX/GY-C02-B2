/**
 * @file    main.c
 * @brief   C02-B2 仪表 MCU 主入口
 */
#include "sdk_project_config.h"

#include "bsp_init.h"
#include "drv_init.h"
#include "utility_print_config.h"  /* UTILITY_PRINT_Init */
#include "drv_api/can/can_if.h"
#include "rti.h"
#include "scheduler.h"

#define LOG_NAME  "MAIN"
#include "log.h"

/**
 * @brief   I2C master event callback stub (kept to satisfy SDK linker).
 * @brief   I2C 主设备事件回调占位实现（满足 SDK 链接器要求）
 *
 * @details The SDK's I2C driver references this symbol. Real
 *          events are not yet handled - register a non-trivial
 *          handler when an I2C slave is wired in.
 *
 * @param[in]  event     I2C master event (unused)
 * @param[in]  userData  User context (unused)
 */
void I2C_DRIVER_MASTER_CALLBACK(i2c_master_event_t event, void *userData)
{
    (void)event; (void)userData;
}

/**
 * @brief   UART0 RX DMA completion callback stub.
 * @brief   UART0 RX DMA 完成回调占位实现
 *
 * @param[in]  parameter  User context (unused)
 * @param[in]  status     DMA status (unused)
 */
void DMA_UART0_RX_FUNCTION_CALLBACK(void *parameter, dma_chn_status_t status)
{
    (void)parameter; (void)status;
}

/**
 * @brief   UART0 TX DMA completion callback stub.
 * @brief   UART0 TX DMA 完成回调占位实现
 *
 * @param[in]  parameter  User context (unused)
 * @param[in]  status     DMA status (unused)
 */
void DMA_UART0_TX_FUNCTION_CALLBACK(void *parameter, dma_chn_status_t status)
{
    (void)parameter; (void)status;
}

/**
 * @brief   UART2 RX DMA completion callback stub.
 * @brief   UART2 RX DMA 完成回调占位实现
 *
 * @param[in]  parameter  User context (unused)
 * @param[in]  status     DMA status (unused)
 */
void DMA_UART2_RX_FUNCTION_CALLBACK(void *parameter, dma_chn_status_t status)
{
    (void)parameter; (void)status;
}

/**
 * @brief   UART2 TX DMA completion callback stub.
 * @brief   UART2 TX DMA 完成回调占位实现
 *
 * @param[in]  parameter  User context (unused)
 * @param[in]  status     DMA status (unused)
 */
void DMA_UART2_TX_FUNCTION_CALLBACK(void *parameter, dma_chn_status_t status)
{
    (void)parameter; (void)status;
}

/**
 * @brief   Application entry point
 * @brief   应用入口
 *
 * @details Bring-up sequence:
 *   1. BSP / driver init: bring up clocks, pins, peripheral drivers.
 *   2. RTI counter: start 1 ms tick source.
 *   3. CAN interface: install FLEXCAN driver + ring buffer.
 *   4. Scheduler: calls every module's init() (which reads signals
 *      etc. and registers ISRs as needed).
 *   5. If KL15 is already on (cold-boot case), broadcast IGN ON so
 *      every module's on_ign_on() runs.
 *   6. Enter the main super-loop with __WFI() idle to save power
 *      between interrupts.
 *
 * @return  int  Never returns (embedded)
 */
int main(void)
{
    /* BSP brings up clocks, pins, DMA, power, WDG and NVIC.
     * After it returns, the LINFlexD UART clock is configured,
     * so we can call UTILITY_PRINT_Init here (before DRV_Init)
     * and start seeing LOG_* output from the very first marker
     * below. */
    (void)BSP_Init();
    (void)UTILITY_PRINT_Init();
    LOG_I("=== C02-B2 boot (post-BSP, UART up) ===");

    /* DRV init: per-peripheral drivers. Any LOG_I called inside
     * DRV_Init is now visible on the UART. */
    (void)DRV_Init();
    LOG_I("=== C02-B2 boot (post-DRV) ===");
    /* Start the 1 ms RTI counter; ISR-driven after this. */
    RTI_Init();
    /* Bring up CAN: FLEXCAN_DRV_Init + InstallEventCallback. */
    (void)CanIf_Init();

    /* Initialize the scheduler: calls every module's init() hook. */
    Scheduler_Init();
    /* Cold-boot broadcast: assume IGN ON so every module's
     * on_ign_on() runs at startup.  Real KL15 detection will be
     * wired in via mod_can once DBC signals are populated. */
    Scheduler_OnIgnOn();
    LOG_I("=== C02-B2 boot OK ===");
    LOG_I("tick=1ms source=SysTick via OSIF");


    /* Super-loop: dispatch every module's tick, then sleep until ISR. */
    for (;;) {
        Scheduler_Run();
        /* Wait For Interrupt: clock-gates the CPU until next 1 ms tick. */
        __WFI();
    }
}
