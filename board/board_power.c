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

/**
 * @brief   Read a raw 12-bit ADC sample from the chosen channel (stub)
 * @brief   从指定通道读取 12-bit 原始 ADC 采样（占位）
 *
 * @details Returns 0 until the ADC chain is wired up. The real
 *          implementation should call ADC_DRV_GetConvResultOnce()
 *          or the equivalent non-blocking API.
 *
 * @param[in]  channel  ADC channel index (unused)
 *
 * @return  u16  Raw ADC value (0..4095 for 12-bit)
 */
u16 Board_ADC_ReadRaw(u8 channel)
{
    (void)channel;
    /* TODO: replace with ADC_DRV_GetConvResultOnce() once ADC chain is wired. */
    return 0u;
}

/**
 * @brief   Convert a raw ADC count to mV using the voltage divider ratio
 * @brief   通过分压比将原始 ADC 计数转换为 mV
 *
 * @details Formula: Vadc = raw / 4096 * VREF_MV
 *          Vin     = Vadc * RATIO
 *          =>      Vin_mV = raw * VREF_MV * RATIO / 4096
 *
 *          The intermediate calculation uses u32 to avoid 16-bit
 *          overflow: raw (12-bit) * VREF_MV (5000) * RATIO (5) = 102,400,000
 *          which exceeds u16 range but fits in u32.
 *
 * @param[in]  raw_adc  Raw ADC value (0..4095)
 *
 * @return  u16  Voltage in mV
 */
u16 Board_VoltageDivider_mV(u16 raw_adc)
{
    /* 12-bit ADC:  raw / 4096 * VREF_MV * RATIO. */
    u32 mv = ((u32)raw_adc * BOARD_VREF_MV * BOARD_VDIV_RATIO) / 4096u;
    return (u16)mv;
}

/**
 * @brief   Register the IGN edge ISR with the board's IRQ controller (stub)
 * @brief   将 IGN 边沿 ISR 注册到板级中断控制器（占位）
 *
 * @details Real implementation must configure the GPIO for external
 *          interrupt, set both-edge trigger, install the ISR vector,
 *          and enable the NVIC slot. The provided `isr` callback
 *          runs in interrupt context and must be ISR-safe.
 *
 * @param[in]  isr  Callback to invoke on either edge (unused)
 */
void Board_RegisterIgnIrq(void (*isr)(void))
{
    (void)isr;
    /* TODO: register external interrupt on IGN pin,
     *       forward to isr() on both edges. */
    LOG_W("Board_RegisterIgnIrq: not yet wired");
}
