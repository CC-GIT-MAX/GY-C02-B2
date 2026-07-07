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

/* Per-peripheral init helpers - one per app/drv_api/<periph>/. */
void Lptmr_Init(void);
void Adc_Init(void);
void Etmr_Init(void);
void I2c_Init(void);
void Uart_Init(void);
void Can_Init(void);
void Flash_Init(void);

#define LOG_NAME  "DRV"
#include "log.h"

/**
 * @brief   Initialize every peripheral driver
 *
 * @return  c02b2_result_t  Always C02B2_OK (vendor errors are logged
 *                          inside each per-peripheral helper).
 */
c02b2_result_t DRV_Init(void)
{
    /* Print utility: needed for LOG_* macros to work. */
    UTILITY_PRINT_Init();
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
