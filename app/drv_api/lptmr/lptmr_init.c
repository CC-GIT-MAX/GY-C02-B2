/**
 * @file    lptmr_init.c
 * @brief   Initialize LPTMR instance 0 as the 1 kHz RTI tick source
 */
#include "sdk_project_config.h"

/**
 * @brief   Initialize LPTMR instance 0
 *
 * @details Pulls the timer config from board/lptmr_config.c. RTI_Init()
 *          is invoked later from main.c after DRV_Init() returns, so
 *          the ISR (lpTMR0_IRQHandler, defined in app/rti/rti.c) only
 *          starts driving RTI once main() is ready.
 *
 * @note    The vendor SDK enables the LPTMR0 NVIC line elsewhere;
 *          see board/interrupt_config.c.
 */
void Lptmr_Init(void)
{
    /* startCounter=false so the timer stays halted until first RTI_OnTick1ms
     * request from main(). */
    lpTMR_DRV_Init(0, &LPTMR_Config, false);
}
