/**
 * @file    board_power.h
 * @brief   Board-level power hardware bindings
 *
 * The mod_power module knows nothing about GPIO or ADC driver.
 * The board layer (this file + the corresponding .c) provides:
 *   - the ADC channel numbers to sample
 *   - the GPIO index + bit mask for IGN edge interrupt
 *   - any debounce / divider constants to convert raw ADC to mV
 *
 * Fill in the real values once the schematic is finalized.
 */
#ifndef LBX_BOARD_POWER_H
#define LBX_BOARD_POWER_H

#include "types.h"

/* ADC channels for KL30 (battery) and IGN (KL15) voltage dividers.
 * Default to template values; override when board is wired up. */
#ifndef BOARD_ADC_CH_KL30
  #define BOARD_ADC_CH_KL30    0
#endif
#ifndef BOARD_ADC_CH_IGN
  #define BOARD_ADC_CH_IGN     1
#endif

/* KL30 / IGN voltage divider ratio (Vin = Vadc * RATIO).
 * Default assumes 12-bit ADC @ 5V Vref, divider 1:5  -> RATIO = 5. */
#ifndef BOARD_VDIV_RATIO
  #define BOARD_VDIV_RATIO     5u
#endif
#ifndef BOARD_VREF_MV
  #define BOARD_VREF_MV        5000u
#endif

/* Power thresholds (mV) */
#define PWR_UV2_ENTER_MV       6500u     /* < 6.5V  -> latch off       */
#define PWR_UV2_EXIT_MV        7000u
#define PWR_UV1_ENTER_MV       9000u     /* < 9.0V  -> warn only       */
#define PWR_UV1_EXIT_MV        9500u
#define PWR_OV1_ENTER_MV      18000u
#define PWR_OV1_EXIT_MV       17500u
#define PWR_OV2_ENTER_MV      20000u     /* > 20V   -> latch off       */

/* IGN debounce: 3 consecutive samples >= threshold to assert
 * IGN_ON.  3 samples * 10ms = 30ms. */
#define PWR_IGN_DEBOUNCE_TICK  3u

/* Hardware access (filled by board layer). */

/**
 * @brief   Read a raw 12-bit ADC sample from the chosen channel
 * @brief   从指定通道读取 12-bit 原始 ADC 采样
 *
 * @param[in]  channel  ADC channel index
 *
 * @return  u16  Raw ADC value (0..4095)
 */
u16  Board_ADC_ReadRaw(u8 channel);

/**
 * @brief   Convert a raw ADC count to mV using the voltage divider ratio
 * @brief   通过分压比将原始 ADC 计数转换为 mV
 *
 * @param[in]  raw_adc  Raw ADC value
 *
 * @return  u16  Voltage in mV
 */
u16  Board_VoltageDivider_mV(u16 raw_adc);

/**
 * @brief   Register the IGN edge ISR with the board's IRQ controller
 * @brief   将 IGN 边沿 ISR 注册到板级中断控制器
 *
 * @param[in]  isr  Callback to invoke on either edge
 */
void Board_RegisterIgnIrq(void (*isr)(void));

#endif /* LBX_BOARD_POWER_H */
