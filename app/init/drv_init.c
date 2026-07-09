/**
 * @file    drv_init.c
 * @brief   Top-level peripheral driver init dispatcher
 *
 * After the refactor, this file no longer touches vendor SDK drivers
 * directly - it only calls the per-peripheral init helpers exposed
 * by app/drv_api/<periph>/<periph>_init.c. Each helper pulls its own
 * config from board/<periph>_config.c, so this file stays a flat list
 * of init calls and the LOG_I marker.
 *
 * The order is preserved verbatim from the pre-refactor file because
 * some drivers have soft ordering constraints (UTILITY_PRINT before
 * any LOG, LPTMR before RTI, etc.). When you add a new driver, append
 * its helper here and document any ordering dependency in the helper.
 */
#include "drv_init.h"

#include "sdk_project_config.h"

/* Per-peripheral init helpers - one per app/drv_api/<periph>/.   *
 * Each is declared in drv_api/<periph>/<periph>.h and defined in    *
 * drv_api/<periph>/<periph>.c (the older <periph>_init.c files     *
 * were merged into those .c/.h pairs). */

/* Drivers not used in the current build are still listed here so    *
 * that adding a new module only touches this dispatcher. */

#include "drv_api/lptmr/lptmr.h"   /* Lptmr_Init (1 kHz RTI tick source)  */
#include "drv_api/adc/adc.h"       /* Adc_Init (converter config)         */
#include "drv_api/etmr/etmr.h"     /* Etmr_Init (counter + PWM channels)  */
#include "drv_api/i2c/i2c.h"       /* I2c_Init (master mode)              */
#include "drv_api/uart/uart.h"     /* Uart_Init (LINFlexD UART0)          */
#include "drv_api/can/can_if.h"    /* Can_Init (FlexCAN1 + FlexCAN2)      */
#include "drv_api/flash/flash.h"   /* Flash_Init (used by app/storage/kv) */

#define LOG_NAME  "DRV"
#include "log.h"

/**
 * @brief   Initialize every peripheral driver
 * @brief   初始化全部外设驱动
 *
 * @return  c02b2_result_t  Always C02B2_OK (vendor errors are logged
 *                          inside each per-peripheral helper).
 */
c02b2_result_t DRV_Init(void)
{
    /* UTILITY_PRINT_Init() is intentionally NOT called here - it
     * lives in main.c right after BSP_Init so that the LOG_*
     * markers below are visible from the very first one. */
    /* Per-peripheral inits - order is significant for the items
     * noted in each helper's file header. */
    Lptmr_Init();
    Adc_Init();
    Etmr_Init();
    I2c_Init();
    Uart_Init();
    Can_Init();
    Flash_Init();
    LOG_I("DRV init OK");
    return C02B2_OK;
}
