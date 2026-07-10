/**
 * @file    fpu.h
 * @brief   FPU (Cortex-M33 FPU + YTM CIM FPU exception) bring-up API
 * @brief   FPU 启用与异常初始化接口
 *
 * Three things must happen to use the FPU on this chip:
 *   1. SCB->CPACR enable CP10/CP11 full access (so the CPU can
 *      actually issue FP instructions).  The SDK already does this
 *      in SystemInit() under #ifdef ENABLE_FPU; Fpu_Init()
 *      re-applies it (idempotent |=) so the routine is also
 *      safe for builds that skipped ENABLE_FPU.
 *   2. Unlock IPC CTRL[123] and set bit 0 (CIM IPC source routing
 *      for the FPU exception).  Without this the FPU exception is
 *      generated but never reaches NVIC.
 *   3. NVIC ISER bit 17 (FPU_IRQn) + CIM->FPUIE = 0x3F to enable
 *      all six FPU exceptions (IOC / DZC / UFC / OFC / IDC / IXC).
 *      The strong FPU_IRQHandler in app/drv_api/fpu/fpu.c replaces
 *      the weak stub in board/vector_table_copy.c; it bumps a
 *      counter readable via Fpu_GetExceptionCount() (no logging
 *      inside the ISR).
 */
#ifndef C02B2_DRV_API_FPU_H
#define C02B2_DRV_API_FPU_H

#include "types.h"
#include "result.h"

/* SCB->CPACR bit fields (ARMv8-M Architecture Reference Manual, B1.4.4).
 *   CP10 occupies bits [23:22], CP11 occupies bits [21:20].
 *   Encoding 0b11 in each pair = Full access (privileged + unprivileged).
 *   CMSIS does not provide bit-field macros for CPACR so we define them
 *   locally to remove the magic 20 / 22 from fpu.c. */
#define FPU_CPACR_CP10_FULL  ((u32)3u << 20)  /**< CP10 full access (FPU ops)   */
#define FPU_CPACR_CP11_FULL  ((u32)3u << 22)  /**< CP11 full access (FPU regs)  */

/**
 * @brief   Enable the FPU coprocessor and route FPU exceptions to NVIC
 * @brief   启用 FPU 协处理器并把 FPU 异常路由到 NVIC
 *
 * @details Idempotent.  CPACR is normally already enabled by
 *          SystemInit() under ENABLE_FPU; this routine re-applies
 *          it (|=, never clobbers other bits) for builds that
 *          skipped ENABLE_FPU.
 *
 *          Sets IPC CTRL[123] to unlock + enable, NVIC ISER bit 17
 *          (FPU_IRQn), and CIM->FPUIE to 0x3F (all six exception
 *          sources).  The FPU IRQ handler is the strong symbol
 *          defined in fpu.c which counts exceptions (no logging
 *          inside the ISR).
 *
 *          Call once from DRV_Init(), before any module performs FP
 *          math.
 *
 * @return  c02b2_result_t
 * @retval  C02B2_OK  Always (FPU is hardware-managed, no failure mode)
 */
c02b2_result_t Fpu_Init(void);

/**
 * @brief   Cumulative count of FPU exceptions raised since boot
 * @brief   自启动以来 FPU 异常的累计计数
 *
 * @details Each FPU exception increments this counter from inside
 *          the FPU IRQ handler.  Useful for SOC / diag to detect
 *          silent FP faults without having to wire LOG output.
 *
 * @return  u32  Total exception count
 */
u32 Fpu_GetExceptionCount(void);

#endif /* C02B2_DRV_API_FPU_H */
