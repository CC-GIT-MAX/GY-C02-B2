/**
 * @file    etmr_init.c
 * @brief   Initialize eTMR channels 0 (counter) and 3 (PWM)
 */
#include "sdk_project_config.h"

/**
 * @brief   Initialize eTMR channels used by the cluster
 *
 * @details Channel 0 in counter mode is used as a general timing source.
 *          Channel 3 in PWM mode drives the stepper-motor coils via the
 *          board pin-mux setup. Both configs come from board/etmr_config.c.
 */
void Etmr_Init(void)
{
    eTMR_DRV_Init(0, &ETMR_CM_Config0, &ETMR_CM_Config0_State);
    eTMR_DRV_InitPwm(3, &ETMR3_PWM_Config0);
}
