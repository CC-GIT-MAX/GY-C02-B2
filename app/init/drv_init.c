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
 * Each is defined in its own drv_api/<periph>/<periph>_init.c;    *
 * the briefs below are short pointers to the full implementation. */
/**
 * @brief   Initialize LPTMR instance 0 (1 kHz RTI tick source).
 * @brief   初始化 LPTMR 实例 0（1 kHz RTI 时钟源）
 *
 * @see     app/drv_api/lptmr/lptmr_init.c
 */
void Lptmr_Init(void);
/**
 * @brief   Initialize ADC instance 0 (converter config).
 * @brief   初始化 ADC 实例 0（转换器配置）
 *
 * @see     app/drv_api/adc/adc_init.c
 */
void Adc_Init(void);
/**
 * @brief   Initialize eTMR channels 0 + 3 (counter + PWM).
 * @brief   初始化 eTMR 通道 0 + 3（计数 + PWM）
 *
 * @see     app/drv_api/etmr/etmr_init.c
 */
void Etmr_Init(void);
/**
 * @brief   Initialize I2C instance 0 in master mode.
 * @brief   初始化 I2C 实例 0（主设备模式）
 *
 * @see     app/drv_api/i2c/i2c_init.c
 */
void I2c_Init(void);
/**
 * @brief   Initialize LINFlexD UART instance 0 (factory comms).
 * @brief   初始化 LINFlexD UART 实例 0（工厂通信）
 *
 * @see     app/drv_api/uart/uart_init.c
 */
void Uart_Init(void);
/**
 * @brief   Initialize both FlexCAN instances (1=private, 2=public).
 * @brief   初始化 两个 FlexCAN 实例（1=私域，2=公域）
 *
 * @see     app/drv_api/can/can_init.c
 */
void Can_Init(void);
/**
 * @brief   Initialize FLASH instance 0 (used by app/storage/kv).
 * @brief   初始化 FLASH 实例 0（被 app/storage/kv 使用）
 *
 * @see     app/drv_api/flash/flash_init.c
 */
void Flash_Init(void);

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
