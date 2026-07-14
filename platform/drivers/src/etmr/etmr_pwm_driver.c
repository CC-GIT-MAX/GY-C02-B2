/*
 * Copyright 2020-2025 Yuntu Microelectronics Co., Ltd.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*!
 * @file etmr_pwm_driver.c
 * @version 1.4.1
 */

#include "etmr_pwm_driver.h"
#include "etmr_hw_access.h"
#include "interrupt_manager.h"


static status_t eTMR_DRV_CalculateDutyCycle(uint32_t period,
                                            etmr_pwm_update_option_t typeOfUpdate,
                                            etmr_pwm_align_mode_t alignMode,
                                            uint32_t dutyCycle,
                                            uint32_t offset,
                                            uint32_t *firstEdge,
                                            uint32_t *secondEdge);

/*FUNCTION**********************************************************************
 *
 * Function Name : eTMR_DRV_InitPwmChannel
 * Description   : This function will initialize the PWM channels about the initial
 *                value, polarity, dead time, double switch, complementary mode,
 *                pwm source invert, pwm mode, val0 and val1 match interrupt enable.
 *
 * Implements: eTMR_DRV_InitPwmChannel_Activity
 *END**************************************************************************/
status_t eTMR_DRV_InitPwmChannel(uint32_t instance, const etmr_pwm_param_t *param)
{
    eTMR_Type *etmrBase = g_etmrBase[instance];
    etmr_state_t *state = etmrStatePtr[instance];
    status_t status = STATUS_SUCCESS;
    uint8_t index;
    uint8_t channelId;
    uint8_t channelPair;

    /* Set channel initial output */
    for (index = 0U; index < param->nNumPwmChannels; index++)
    {
        channelId = param->pwmChannelConfig[index].hwChannelId;
        eTMR_SetChnOutInitVal(etmrBase, channelId, (uint32_t)param->pwmChannelConfig[index].channelInitVal);
        eTMR_InitChnOutput(etmrBase, channelId);
    }

#if defined(FEATURE_eTMR_DEADTIME_CONFIG_EACH_CHANNEL) && (FEATURE_eTMR_DEADTIME_CONFIG_EACH_CHANNEL == 0U)
    if (param->deadtimeConfig != NULL)
    {
        /* Configure dead time */
        eTMR_SetDeadtimePrs(etmrBase, param->deadtimeConfig->prs);
        eTMR_SetOddDeadtime(etmrBase, param->deadtimeConfig->oddDeadTime);
        eTMR_SetEvenDeadtime(etmrBase, param->deadtimeConfig->evenDeadTime);
    }
#endif

    /* Configure independent PWM channels */
    for (index = 0U; index < param->nNumPwmChannels; index++)
    {
        channelId = param->pwmChannelConfig[index].hwChannelId;
        channelPair = (uint8_t)(channelId >> 1U);

        /* Set the channel as pwm mode */
        eTMR_SetChnMode(etmrBase, channelId, eTMR_PWM_MODE);

#if defined(FEATURE_eTMR_HAS_CHANNEL_VALUE_MATCH_INTERRUPT_IN_PWM_MODE) && \
           (FEATURE_eTMR_HAS_CHANNEL_VALUE_MATCH_INTERRUPT_IN_PWM_MODE == 1)
        if (param->pwmChannelConfig[index].interruptEnable)
        {
            eTMR_DRV_EnableChnInt(instance, channelId);
            INT_SYS_EnableIRQ(g_etmrIrqId[instance][channelId]);
        }
#endif

#if FEATURE_eTMR_HAS_DOUBLE_SWITCH
        /* Enable double switch */
        if (param->pwmChannelConfig[index].enableDoubleSwitch)
        {
            eTMR_DRV_SetChnDoubleSwitch(instance, channelPair, true);
        }
#endif

        /* set complementary mode */
        if (param->pwmChannelConfig[index].enableSecondChannelOutput)
        {
            /* Set odd channel as pwm mode */
            eTMR_SetChnMode(etmrBase, channelId + 1U, eTMR_PWM_MODE);

#if FEATURE_eTMR_DEADTIME_CONFIG_EACH_CHANNEL
            /* Configure dead time, and enable complementary channel. */
            eTMR_SetChnDeadtime(etmrBase, channelId, param->pwmChannelConfig[index].evenDeadTime);
            eTMR_SetChnDeadtime(etmrBase, channelId + 1U, param->pwmChannelConfig[index].oddDeadTime);
#endif

            /* Enable complementary mode */
            eTMR_DRV_SetChnCompMode(instance, channelPair, PWM_COMPLEMENTARY_MODE);

            if (param->pwmChannelConfig[index].secondChannelPolarity == eTMR_POLARITY_INVERT)
            {
                /* Invert the other channel pwm output */
                eTMR_IsInvertChnOutputPol(etmrBase, channelId + 1U, true);
            }

            state->etmrChnMode[channelId] = eTMR_STATE_PWM_COMPLEMENTARY;
        }
        else
        {
            /* Set the polarity */
            if (param->pwmChannelConfig[index].polarity == eTMR_POLARITY_INVERT)
            {
                eTMR_IsInvertChnOutputPol(etmrBase, channelId, true);
            }

            /* Set the PWM source invert */
            if (param->pwmChannelConfig[index].pwmSrcInvert)
            {
                eTMR_SetChnPwmSrc(etmrBase, channelId, 0x01U);
            }

            state->etmrChnMode[channelId] = eTMR_STATE_PWM_INDEPENDENT;
        }
    }

    return status;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : eTMR_DRV_InitPwmDutyCycleChannel
 * Description   : This function will initialize the duty cycle for the PWM signal
 *                 at the initialization.
 *
 * Implements: eTMR_DRV_InitPwmDutyCycleChannel_Activity
 *END**************************************************************************/
status_t eTMR_DRV_InitPwmDutyCycleChannel(uint32_t instance, const etmr_pwm_param_t *param)
{
    status_t status = STATUS_SUCCESS;
    etmr_state_t *state = etmrStatePtr[instance];
    uint8_t index;
    uint8_t hwChannel;
    uint32_t dutyCyclePercent;

    for (index = 0U; index < param->nNumPwmChannels; index++)
    {
        hwChannel = param->pwmChannelConfig[index].hwChannelId;
        dutyCyclePercent = param->pwmChannelConfig[index].dutyCycle;
        state->alignMode[hwChannel] = param->pwmChannelConfig[index].align;
        state->typeOfUpdate[hwChannel] = param->pwmChannelConfig[index].typeOfUpdate;

        if (state->alignMode[hwChannel] != eTMR_PWM_ASYMMETRICAL)
        {
            state->offset[hwChannel] = 0U;
        }
        else
        {
            state->offset[hwChannel] = param->pwmChannelConfig[index].offset;
        }

        status = eTMR_DRV_UpdatePwmChannel(instance, hwChannel, dutyCyclePercent, state->offset[hwChannel]);
    }

    return status;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : eTMR_DRV_InitFault
 * Description   : This function will initialize fault parameters.
 *
 * Implements    : eTMR_DRV_InitFault_Activity
 *END**************************************************************************/
status_t eTMR_DRV_InitFault(uint32_t instance, const etmr_fault_param_t *param)
{
    DEV_ASSERT(instance < eTMR_INSTANCE_COUNT);
    DEV_ASSERT(param != NULL);
    status_t status = STATUS_SUCCESS;
    uint8_t fltChannel;
    uint16_t faultValue = 0;
    uint8_t faultChnNum = (uint8_t)FEATURE_eTMR_FAULT_CHANNELS;
    eTMR_Type *etmrBase = g_etmrBase[instance];

    if (NULL != param)
    {
        for (uint8_t i = 0; i < g_etmrChannelNum[instance]; i++)
        {
            faultValue |= ((uint16_t)param->safeState[i] << (i * 2U));
        }

        /* Set fault value */
        eTMR_SetChnOutFaultVal(etmrBase, faultValue);

        /* Set fault channel */
        for (fltChannel = 0U; fltChannel < faultChnNum; fltChannel++)
        {
            if (true == param->etmrFaultChannelParam[fltChannel].faultChannelEnabled)
            {
                /* Configure fault filter parameters */
                eTMR_SetFaultFilterSampleCnt(etmrBase, param->faultFilterSampleCounter);
                eTMR_SetFaultFilterSamplePeriod(etmrBase, param->faultFilterSamplePeriod);
                eTMR_SetFaultInputStrentch(etmrBase, param->faultInputStrentch);

                if (param->faultMode == eTMR_FAULT_WITHOUT_CLK)
                {
                    eTMR_EnableFaultCombLogic(etmrBase);
                }

                /* pwm automatic recovery mode after detected */
                eTMR_SetPwmAutoRecoveryMode(etmrBase, (uint8_t)param->pwmAutoRecoveryMode);
                /* select pwm recovery opportunity when fault event occurs */
                eTMR_SetPwmRecoveryOpportunity(etmrBase, (uint8_t)param->pwmRecoveryOpportunity);

                /* Set fault input polarity */
                eTMR_DRV_SetFaultChnPol(instance,
                                        fltChannel,
                                        (uint32_t)(param->etmrFaultChannelParam[fltChannel].faultInputPolarity));

                /* Enable fault channel */
                eTMR_DRV_SetFaultChnEnable(instance, fltChannel, true);
            }
        }

        /* Set Fault Interrupt Enable */
        if (true == (param->pwmFaultInterrupt))
        {
            eTMR_SetFaultIntEnable(etmrBase, true);
        }
    }

    return status;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : eTMR_DRV_InitPwm
 * Description   : This function will initialize the eTMR module in PWM mode.
 *
 * Implements    : eTMR_DRV_InitPwm_Activity
 *END**************************************************************************/
status_t eTMR_DRV_InitPwm(uint32_t instance, const etmr_pwm_param_t *param)
{
    DEV_ASSERT(instance < eTMR_INSTANCE_COUNT);
    DEV_ASSERT(param != NULL);
    status_t status = STATUS_SUCCESS;
    etmr_state_t *state = etmrStatePtr[instance];
    eTMR_Type *etmrBase = g_etmrBase[instance];

    if (NULL != state)
    {
        /* Calculate frequency of the given eTMR hardware module - all channels will run at the same frequency */
        state->etmrPeriod = eTMR_DRV_ConvertFreqToPeriodTicks(instance, param->uFrequencyHZ);

#if FEATURE_eTMR_HAS_CNT_INIT_SRC
        /* Configure counter initial value */
        if (param->counterInitValFromInitReg)
        {
            /* Set counter just count from INIT register */
            eTMR_SetCounterInitValSrc(etmrBase, (uint8_t)COUNTER_VAL_FROM_INIT);
            /* Configure INIT register, count from 0 */
            eTMR_SetInitVal(etmrBase, 0U);
        }
        else
#endif
        {
            /* Configure CNT register, when enabling, counter will start to count from CNT register, and then from INIT register */
            eTMR_SetCntVal(etmrBase, param->cntVal);
#if FEATURE_eTMR_HAS_INIT_REG
            /* Configure INIT register, count from INIT for LE0, ME0, MD1
               But for other devices, counter will count from INIT+1 starting from the second cycle */
            eTMR_SetInitVal(etmrBase, 0U);
#endif
        }

        /* Configure faults (if faults were enabled) */
        if (param->faultConfig != NULL)
        {
            (void)eTMR_DRV_InitFault(instance, param->faultConfig);
        }

        /* Write MOD register with the value of the period */
        state->etmrModValue = state->etmrPeriod - 1U
#if defined(FEATURE_eTMR_COUNT_FROM_INIT_PLUS_ONE) && (FEATURE_eTMR_COUNT_FROM_INIT_PLUS_ONE == 1U)
                                + 1U
#endif
                                ;
        eTMR_SetMod(etmrBase, state->etmrModValue);

        /* Initialize the duty cycle */
        status = eTMR_DRV_InitPwmDutyCycleChannel(instance, param);
        if (status == STATUS_SUCCESS)
        {
            /* Configure PWM channels */
            status = eTMR_DRV_InitPwmChannel(instance, param);
        }
    }
    else
    {
        status = STATUS_ERROR;
    }

    return status;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : eTMR_DRV_DeinitPwm
 * Description   : This function will stop all PWM channels.
 *
 * Implements    : eTMR_DRV_DeinitPwm_Activity
 *END**************************************************************************/
status_t eTMR_DRV_DeinitPwm(uint32_t instance)
{
    DEV_ASSERT(instance < eTMR_INSTANCE_COUNT);

    uint8_t channel;
    eTMR_Type *etmrBase = g_etmrBase[instance];
    status_t status = STATUS_SUCCESS;

    eTMR_ClearLdok(etmrBase);
    eTMR_Disable(etmrBase);
#if FEATURE_eTMR_HAS_INIT_REG
    /* Configure INIT register */
    eTMR_SetInitVal(etmrBase, 0U);
#endif
    eTMR_SetChnOutSwVal(etmrBase, 0U);
#if defined(FEATURE_eTMR_DEADTIME_CONFIG_EACH_CHANNEL) && (FEATURE_eTMR_DEADTIME_CONFIG_EACH_CHANNEL == 0U)
    eTMR_SetDeadtimePrs(etmrBase, 0U);
    eTMR_SetOddDeadtime(etmrBase, 0U);
    eTMR_SetEvenDeadtime(etmrBase, 0U);
#endif

    /* Deinit Channel mode pwm */
    for (channel = 0; channel < g_etmrChannelNum[instance]; channel++)
    {
        eTMR_SetChnOutInitVal(etmrBase, channel, 0U);
        eTMR_SetChnMode(etmrBase, channel, eTMR_CHANNEL_DISABLE);
        eTMR_SetChnPwmSrc(etmrBase, channel, 0);
        eTMR_IsInvertChnOutputPol(etmrBase, channel, false);
#if FEATURE_eTMR_DEADTIME_CONFIG_EACH_CHANNEL
        eTMR_SetChnDeadtime(etmrBase, channel, 0U);
#endif
        eTMR_SetChnVal0(etmrBase, channel, 0U);
        eTMR_SetChnVal1(etmrBase, channel, 0U);
    }

    eTMR_SetMod(etmrBase, 0U);
#if FEATURE_eTMR_HAS_MID
    eTMR_SetMid(etmrBase, 0U);
#endif

    return status;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : eTMR_DRV_UpdatePwmPeriodAndDuty
 * Description   : This function will update the PWM period and duty cycle fast.
 *
 * Implements    : eTMR_DRV_UpdatePwmPeriodAndDuty_Activity
 *END**************************************************************************/
status_t eTMR_DRV_UpdatePwmPeriodAndDuty(uint32_t instance,
                                         uint8_t channel,
                                         uint32_t periodTicks,
                                         uint32_t dutyCycleTicks,
                                         uint32_t offset)
{
    DEV_ASSERT(instance < eTMR_INSTANCE_COUNT);
    DEV_ASSERT(channel < g_etmrChannelNum[instance]);

    eTMR_Type *etmrBase = g_etmrBase[instance];
    etmr_state_t *state = etmrStatePtr[instance];
    /* Hold the original update type */
    etmr_pwm_update_option_t updateType = state->typeOfUpdate[channel];
    /* Modify the update type as in ticks for this function */
    state->typeOfUpdate[channel] = eTMR_PWM_UPDATE_IN_TICKS;
    status_t status = STATUS_SUCCESS;

    state->etmrPeriod = periodTicks;
    state->etmrModValue = periodTicks - 1U
#if defined(FEATURE_eTMR_COUNT_FROM_INIT_PLUS_ONE) && (FEATURE_eTMR_COUNT_FROM_INIT_PLUS_ONE == 1U)
                              + 1U
#endif
                              ;
    eTMR_SetMod(etmrBase, state->etmrModValue);

    status = eTMR_DRV_UpdatePwmChannel(instance, channel, dutyCycleTicks, offset);

    /* Restore the original update type */
    state->typeOfUpdate[channel] = updateType;

    return status;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : eTMR_DRV_UpdatePwmChannel
 * Description   : The function will update the duty cycle of the PWM.
 *
 * Implements    : eTMR_DRV_UpdatePwmChannel_Activity
 *END**************************************************************************/
status_t eTMR_DRV_UpdatePwmChannel(uint32_t instance,
                                   uint8_t channel,
                                   uint32_t dutyCycle,
                                   uint32_t offset)
{
    DEV_ASSERT(instance < eTMR_INSTANCE_COUNT);
    DEV_ASSERT(channel < g_etmrChannelNum[instance]);
    uint32_t hwFirstEdge = 0U;
    uint32_t hwSecondEdge = 0U;
    uint32_t etmrPeriod;
    eTMR_Type *etmrBase = g_etmrBase[instance];
    etmr_state_t *state = etmrStatePtr[instance];
    status_t status = STATUS_SUCCESS;

    /* Get the latest period in the MOD register */
    etmrPeriod = state->etmrPeriod;

    /* Store the PWM duty cycle in the state structure */
    state->dutyCycle[channel] = dutyCycle;
    state->offset[channel] = offset;

    // Calculate the val0 and val1
    status = eTMR_DRV_CalculateDutyCycle(etmrPeriod,
                                         state->typeOfUpdate[channel],
                                         state->alignMode[channel],
                                         dutyCycle,
                                         offset,
                                         &hwFirstEdge,
                                         &hwSecondEdge);
    
    eTMR_SetChnVal0(etmrBase, channel, hwFirstEdge);
    eTMR_SetChnVal1(etmrBase, channel, hwSecondEdge);

    return status;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : eTMR_DRV_UpdatePwmPeriod
 * Description   : This function will update the new frequency in Hz and it will 
 *                 keep the duty cycle of channels which have been configured
 *
 * Implements : eTMR_DRV_UpdatePwmPeriod_Activity
 *END**************************************************************************/
status_t eTMR_DRV_UpdatePwmPeriod(uint32_t instance, etmr_pwm_period_unit_t unit, uint32_t newPeriod)
{
    DEV_ASSERT(instance < eTMR_INSTANCE_COUNT);
    DEV_ASSERT(newPeriod != 0U);
    uint32_t hwFirstEdge = 0U;
    uint32_t hwSecondEdge = 0U;
    uint32_t etmrPeriod = 0U;
    status_t status = STATUS_SUCCESS;
    eTMR_Type *etmrBase;
    etmr_state_t *state;

    if (instance < eTMR_INSTANCE_COUNT)
    {
        etmrBase = g_etmrBase[instance];
        state = etmrStatePtr[instance];
        /* Check the type of update for period in PWM mode */
        if (unit == eTMR_PWM_PERIOD_IN_TICKS)
        {
#if defined(CPU_YTM32B1MD1) || defined(CPU_YTM32B1HA0) || defined(CPU_YTM32B1ME0)
            if (instance != 3U)
#endif
            {
                if (newPeriod > 0xFFFFU)
                {
                    status = STATUS_ERROR;
                }
            }
            etmrPeriod = newPeriod;
        }
        else
        {
            if (newPeriod <= state->etmrSourceClockFrequency)
            {
                etmrPeriod = eTMR_DRV_ConvertFreqToPeriodTicks(instance, newPeriod);
            }
            else
            {
                status = STATUS_ERROR;
            }
        }
    
        state->etmrModValue = etmrPeriod - 1U
#if defined(FEATURE_eTMR_COUNT_FROM_INIT_PLUS_ONE) && (FEATURE_eTMR_COUNT_FROM_INIT_PLUS_ONE == 1U)
                              + 1U
#endif
                              ;
        /* Store the PWM period in the state structure */
        state->etmrPeriod = etmrPeriod;
        /* Set the new value into MOD register */
        eTMR_SetMod(etmrBase, state->etmrModValue);
    
        for (uint8_t channel = 0; channel < g_etmrChannelNum[instance]; channel++)
        {
            status = eTMR_DRV_CalculateDutyCycle(etmrPeriod,
                                                 state->typeOfUpdate[channel],
                                                 state->alignMode[channel],
                                                 state->dutyCycle[channel],
                                                 state->offset[channel],
                                                 &hwFirstEdge,
                                                 &hwSecondEdge);
    
            eTMR_SetChnVal0(etmrBase, channel, hwFirstEdge);
            eTMR_SetChnVal1(etmrBase, channel, hwSecondEdge);
        }
    }
    else
    {
        status = STATUS_ERROR;
    }

    return status;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : eTMR_DRV_CalculateDutyCycle
 * Description   : This function will calculate the val0 and val1 value.
 *
 * Implements    : eTMR_DRV_CalculateDutyCycle_Activity
 *END**************************************************************************/
static status_t eTMR_DRV_CalculateDutyCycle(uint32_t period,
                                            etmr_pwm_update_option_t typeOfUpdate,
                                            etmr_pwm_align_mode_t alignMode,
                                            uint32_t dutyCycle,
                                            uint32_t offset,
                                            uint32_t *firstEdge,
                                            uint32_t *secondEdge)
{
    status_t status = STATUS_SUCCESS;

    if (alignMode == eTMR_PWM_RIGHT_EDGE_ALIGN)
    {
        if (dutyCycle == 0U)
        {
            #if defined(FEATURE_eTMR_COUNT_FROM_INIT_PLUS_ONE) && (FEATURE_eTMR_COUNT_FROM_INIT_PLUS_ONE == 1U)
            *firstEdge = 1U;
            *secondEdge = 1U;
            #else
            *firstEdge = 0U;
            *secondEdge = 0U;
            #endif
        }
        else
        {
            if (typeOfUpdate == eTMR_PWM_UPDATE_IN_DUTY_CYCLE)
            {
                if (dutyCycle == 0x8000U)
                {
                    #if defined(CPU_YTM32B1MD2)
                    *firstEdge = 0U;
                    *secondEdge = period;
                    #else
                    #if defined(FEATURE_eTMR_COUNT_FROM_INIT_PLUS_ONE) && (FEATURE_eTMR_COUNT_FROM_INIT_PLUS_ONE == 1U)
                    *firstEdge = 1U;
                    *secondEdge = period;
                    #else
                    *firstEdge = 0U;
                    *secondEdge = period - 1U;
                    #endif
                    #endif
                }
                else
                {
                    /* For greater resolution the DutyCycle values are in the range [0, eTMR_MAX_DUTY_CYCLE)
                     * where 0 = 0% or PWM always at Low;
                     * a value of 0x4000 is equivalent of 50% DutyCycle. 
                     * There's a small issue when 100% or always at High, so it recommends to use channel mask to output high. */
                    #if defined(FEATURE_eTMR_COUNT_FROM_INIT_PLUS_ONE) && (FEATURE_eTMR_COUNT_FROM_INIT_PLUS_ONE == 1U)
                    *firstEdge = (uint32_t)(((uint64_t)period * ((uint64_t)0x8000U - dutyCycle)) >> eTMR_DUTY_TO_TICKS_SHIFT);
                    *secondEdge = period;
                    #else
                    *firstEdge = (uint32_t)(((uint64_t)period * ((uint64_t)0x8000U - dutyCycle)) >> eTMR_DUTY_TO_TICKS_SHIFT) - 1U;
                    *secondEdge = period - 1U;
                    #endif
                }
            }
            else // eTMR_PWM_UPDATE_IN_TICKS
            {
                if (dutyCycle == period)
                {
                    #if defined(CPU_YTM32B1MD2)
                    *firstEdge = 0U;
                    *secondEdge = period;
                    #else
                    #if defined(FEATURE_eTMR_COUNT_FROM_INIT_PLUS_ONE) && (FEATURE_eTMR_COUNT_FROM_INIT_PLUS_ONE == 1U)
                    *firstEdge = 1U;
                    *secondEdge = period;
                    #else
                    *firstEdge = 0U;
                    *secondEdge = period - 1U;
                    #endif
                    #endif
                }
                else
                {
                    #if defined(FEATURE_eTMR_COUNT_FROM_INIT_PLUS_ONE) && (FEATURE_eTMR_COUNT_FROM_INIT_PLUS_ONE == 1U)
                    *firstEdge = period - dutyCycle;
                    *secondEdge = period;
                    #else
                    *firstEdge = period - dutyCycle - 1U;
                    *secondEdge = period - 1U;
                    #endif
                }
            }
        }
    }
    else if (alignMode == eTMR_PWM_LEFT_EDGE_ALIGN)
    {
        if (dutyCycle == 0U)
        {
            #if defined(FEATURE_eTMR_COUNT_FROM_INIT_PLUS_ONE) && (FEATURE_eTMR_COUNT_FROM_INIT_PLUS_ONE == 1U)
            *firstEdge = 1U;
            *secondEdge = 1U;
            #else
            *firstEdge = 0U;
            *secondEdge = 0U;
            #endif
        }
        else
        {
            if (typeOfUpdate == eTMR_PWM_UPDATE_IN_DUTY_CYCLE)
            {
                if (dutyCycle == 0x8000U)
                {
                    #if defined(CPU_YTM32B1MD2)
                    *firstEdge = 0U;
                    *secondEdge = period;
                    #else
                    #if defined(FEATURE_eTMR_COUNT_FROM_INIT_PLUS_ONE) && (FEATURE_eTMR_COUNT_FROM_INIT_PLUS_ONE == 1U)
                    *firstEdge = 1U;
                    *secondEdge = period;
                    #else
                    *firstEdge = 0U;
                    *secondEdge = period - 1U;
                    #endif
                    #endif
                }
                else
                {
                    #if defined(FEATURE_eTMR_COUNT_FROM_INIT_PLUS_ONE) && (FEATURE_eTMR_COUNT_FROM_INIT_PLUS_ONE == 1U)
                    *firstEdge = 1U;
                    *secondEdge = (uint32_t)(((uint64_t)period * (uint64_t)dutyCycle) >> eTMR_DUTY_TO_TICKS_SHIFT);
                    #else
                    *firstEdge = 0U;
                    *secondEdge = (uint32_t)(((uint64_t)period * (uint64_t)dutyCycle) >> eTMR_DUTY_TO_TICKS_SHIFT) - 1U;
                    #endif
                }
            }
            else // eTMR_PWM_UPDATE_IN_TICKS
            {
                if (dutyCycle == period)
                {
                    #if defined(CPU_YTM32B1MD2)
                    *firstEdge = 0U;
                    *secondEdge = period;
                    #else
                    #if defined(FEATURE_eTMR_COUNT_FROM_INIT_PLUS_ONE) && (FEATURE_eTMR_COUNT_FROM_INIT_PLUS_ONE == 1U)
                    *firstEdge = 1U;
                    *secondEdge = period;
                    #else
                    *firstEdge = 0U;
                    *secondEdge = period - 1U;
                    #endif
                    #endif
                }
                else
                {
                    #if defined(FEATURE_eTMR_COUNT_FROM_INIT_PLUS_ONE) && (FEATURE_eTMR_COUNT_FROM_INIT_PLUS_ONE == 1U)
                    *firstEdge = 1U;
                    *secondEdge = dutyCycle;
                    #else
                    *firstEdge = 0U;
                    *secondEdge = dutyCycle - 1U;
                    #endif
                }
            }
        }
    }
    else if (alignMode == eTMR_PWM_CENTER_ALIGN)
    {
        if (dutyCycle == 0U)
        {
            #if defined(FEATURE_eTMR_COUNT_FROM_INIT_PLUS_ONE) && (FEATURE_eTMR_COUNT_FROM_INIT_PLUS_ONE == 1U)
            *firstEdge = 1U;
            *secondEdge = 1U;
            #else
            *firstEdge = 0U;
            *secondEdge = 0U;
            #endif
        }
        else
        {
            if (typeOfUpdate == eTMR_PWM_UPDATE_IN_DUTY_CYCLE)
            {
                if (dutyCycle == 0x8000U)
                {
                    #if defined(CPU_YTM32B1MD2)
                    *firstEdge = 0U;
                    *secondEdge = period;
                    #else
                    #if defined(FEATURE_eTMR_COUNT_FROM_INIT_PLUS_ONE) && (FEATURE_eTMR_COUNT_FROM_INIT_PLUS_ONE == 1U)
                    *firstEdge = 1U;
                    *secondEdge = period;
                    #else
                    *firstEdge = 0U;
                    *secondEdge = period - 1U;
                    #endif
                    #endif
                }
                else
                {
                    #if defined(FEATURE_eTMR_COUNT_FROM_INIT_PLUS_ONE) && (FEATURE_eTMR_COUNT_FROM_INIT_PLUS_ONE == 1U)
                    *firstEdge = (uint32_t)(((uint64_t)period * ((uint64_t)0x4000U - ((uint64_t)dutyCycle >> 1))) >> eTMR_DUTY_TO_TICKS_SHIFT);
                    *secondEdge = (uint32_t)(((uint64_t)period * ((uint64_t)0x4000U + ((uint64_t)dutyCycle >> 1))) >> eTMR_DUTY_TO_TICKS_SHIFT);
                    #else
                    *firstEdge = (uint32_t)(((uint64_t)period * ((uint64_t)0x4000U - ((uint64_t)dutyCycle >> 1))) >> eTMR_DUTY_TO_TICKS_SHIFT) - 1U;
                    *secondEdge = (uint32_t)(((uint64_t)period * ((uint64_t)0x4000U + ((uint64_t)dutyCycle >> 1))) >> eTMR_DUTY_TO_TICKS_SHIFT) - 1U;
                    #endif
                }
            }
            else // eTMR_PWM_UPDATE_IN_TICKS
            {
                if (dutyCycle == period)
                {
                    #if defined(CPU_YTM32B1MD2)
                    *firstEdge = 0U;
                    *secondEdge = period;
                    #else
                    #if defined(FEATURE_eTMR_COUNT_FROM_INIT_PLUS_ONE) && (FEATURE_eTMR_COUNT_FROM_INIT_PLUS_ONE == 1U)
                    *firstEdge = 1U;
                    *secondEdge = period;
                    #else
                    *firstEdge = 0U;
                    *secondEdge = period - 1U;
                    #endif
                    #endif
                }
                else
                {
                    #if defined(FEATURE_eTMR_COUNT_FROM_INIT_PLUS_ONE) && (FEATURE_eTMR_COUNT_FROM_INIT_PLUS_ONE == 1U)
                    *firstEdge = ((period - dutyCycle) >> 1);
                    *secondEdge = ((period + dutyCycle) >> 1);
                    #else
                    *firstEdge = ((period - dutyCycle) >> 1) - 1U;
                    *secondEdge = ((period + dutyCycle) >> 1) - 1U;
                    #endif
                }
            }
        }
    }
    else // eTMR_PWM_ASYMMETRICAL
    {
        if (dutyCycle == 0U)
        {
            #if defined(FEATURE_eTMR_COUNT_FROM_INIT_PLUS_ONE) && (FEATURE_eTMR_COUNT_FROM_INIT_PLUS_ONE == 1U)
            *firstEdge = 1U;
            *secondEdge = 1U;
            #else
            *firstEdge = 0U;
            *secondEdge = 0U;
            #endif
        }
        else
        {
            if (typeOfUpdate == eTMR_PWM_UPDATE_IN_DUTY_CYCLE)
            {
                if (offset <= (eTMR_MAX_DUTY_CYCLE - dutyCycle))
                {
                    if (offset == 0U)
                    {
                        #if defined(CPU_YTM32B1MD2)
                        *firstEdge = 0U;
                        *secondEdge = period;
                        #else
                        #if defined(FEATURE_eTMR_COUNT_FROM_INIT_PLUS_ONE) && (FEATURE_eTMR_COUNT_FROM_INIT_PLUS_ONE == 1U)
                        *firstEdge = 1U;
                        *secondEdge = period;
                        #else
                        *firstEdge = 0U;
                        *secondEdge = period - 1U;
                        #endif
                        #endif
                    }
                    else
                    {
                        #if defined(FEATURE_eTMR_COUNT_FROM_INIT_PLUS_ONE) && (FEATURE_eTMR_COUNT_FROM_INIT_PLUS_ONE == 1U)
                        *firstEdge = (uint32_t)(((uint64_t)period * (uint64_t)offset) >> eTMR_DUTY_TO_TICKS_SHIFT);
                        *secondEdge = (uint32_t)(((uint64_t)period * ((uint64_t)offset + (uint64_t)dutyCycle)) >> eTMR_DUTY_TO_TICKS_SHIFT);
                        #else
                        *firstEdge = (uint32_t)(((uint64_t)period * (uint64_t)offset) >> eTMR_DUTY_TO_TICKS_SHIFT) - 1U;
                        *secondEdge = (uint32_t)(((uint64_t)period * ((uint64_t)offset + (uint64_t)dutyCycle)) >> eTMR_DUTY_TO_TICKS_SHIFT) - 1U;
                        #endif
                    }
                }
                else
                {
                    status = STATUS_ERROR;
                }
            }
            else
            {
                if (offset <= (period - dutyCycle))
                {
                    if (offset == 0U)
                    {
                        #if defined(CPU_YTM32B1MD2)
                        *firstEdge = 0U;
                        *secondEdge = period;
                        #else
                        #if defined(FEATURE_eTMR_COUNT_FROM_INIT_PLUS_ONE) && (FEATURE_eTMR_COUNT_FROM_INIT_PLUS_ONE == 1U)
                        *firstEdge = 1U;
                        *secondEdge = period;
                        #else
                        *firstEdge = 0U;
                        *secondEdge = period - 1U;
                        #endif
                        #endif
                    }
                    else
                    {
                        #if defined(FEATURE_eTMR_COUNT_FROM_INIT_PLUS_ONE) && (FEATURE_eTMR_COUNT_FROM_INIT_PLUS_ONE == 1U)
                        *firstEdge = offset;
                        *secondEdge = offset + dutyCycle;
                        #else
                        *firstEdge = offset - 1U;
                        *secondEdge = offset + dutyCycle - 1U;
                        #endif
                    }
                }
                else
                {
                    status = STATUS_ERROR;
                }
            }
        }
    }

    return status;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : eTMR_DRV_SyncWithSoftTrigger
 * Description   : This function will generate software trigger to synchronize
 *                 registers(INIT, CHMASK, MOD, MID, CHx_VAL0, CHx_VAL1)
 *
 * Implements : eTMR_DRV_SyncWithSoftTrigger_Activity
 *END**************************************************************************/
void eTMR_DRV_SyncWithSoftTrigger(uint32_t instance)
{
    DEV_ASSERT(instance < eTMR_INSTANCE_COUNT);
    eTMR_Type *etmrBase = g_etmrBase[instance];

    eTMR_SetLdok(etmrBase);

    eTMR_GenSoftwareTrigger(etmrBase, true);
}

/*! @}*/
/*******************************************************************************
* EOF
******************************************************************************/
