/**
 * @file    board_power.c
 * @brief   Board-level power hardware binding implementation
 *
 * Defaults below assume the SDK template; replace with real
 * ADC + GPIO calls once schematic is finalized.
 */
#include "board_power.h"
#include "types.h"

#define LOG_NAME  "BPWR"
#include "log.h"

u16 Board_ADC_ReadRaw(u8 channel)
{
    (void)channel;
    /* TODO: replace with ADC_DRV_GetConvResultOnce() once ADC chain is wired */
    return 0u;
}

u16 Board_VoltageDivider_mV(u16 raw_adc)
{
    /* 12-bit ADC:  raw / 4096 * VREF_MV * RATIO */
    u32 mv = ((u32)raw_adc * BOARD_VREF_MV * BOARD_VDIV_RATIO) / 4096u;
    return (u16)mv;
}

void Board_RegisterIgnIrq(void (*isr)(void))
{
    (void)isr;
    /* TODO: register external interrupt on IGN pin,
     *       forward to isr() on both edges. */
    LOG_W("Board_RegisterIgnIrq: not yet wired");
}
