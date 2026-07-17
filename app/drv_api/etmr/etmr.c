/**
 * @file    etmr.c
 * @brief   eTMR peripheral driver implementation
 * @brief   eTMR 外设驱动实现
 *
 * 模块级初始化以及（未来可能新增的）公共辅助函数均在本文件实现。
 * 取代了较早的 app/drv_api/etmr/etmr_init.c。
 */
#include "etmr.h"
#include "sdk_project_config.h"

#define MOD_NAME  "ETM"
#include "log.h"

status_t Etmr_Init(void)
{
    status_t status = STATUS_ERROR;
    status |= eTMR_DRV_Init(3, &ETMR_CM_Config0, &ETMR_CM_Config0_State);
    status |= eTMR_DRV_InitPwm(3, &ETMR3_PWM_Config0);

    eTMR_DRV_Enable(3);
    return status;
}

status_t Etmr_PwmSetVALUE(uint32 instance,uint8 channel,uint32 dutyCycle,uint8 enable)
{
    status_t status = STATUS_ERROR;
    if(0==enable)
    {
        eTMR_DRV_Disable(instance);
    }
    else
    {
        dutyCycle*=32;    //可点亮范围在1~32760 超过息屏

        eTMR_DRV_Enable(instance);
        status |= eTMR_DRV_UpdatePwmChannel(instance,channel,dutyCycle,0);
        status |= eTMR_DRV_SetLdok(instance);
    }
    return status;
}