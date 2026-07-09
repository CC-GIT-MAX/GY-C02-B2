/**
 * @file    flash_init.c
 * @brief   Initialize FLASH instance 0 (used by app/storage/kv)
 */
#include "sdk_project_config.h"

/**
 * @brief   Initialize FLASH instance 0
 *
 * @details The driver handle is required by app/storage/kv for the
 *          non-volatile key-value store. Keep this init AFTER the
 *          CAN / RTI / UART inits so a slow first-time flash erase
 *          cannot stall early bring-up logging.
 */
void Flash_Init(void)
{
    FLASH_DRV_Init(0, &flash_config0, &flash_config0_State);
}
