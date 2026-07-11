/**
 * @file    fpu.c
 * @brief   FPU (Cortex-M33 FPU + YTM CIM FPU exception) bring-up
 * @brief   FPU 启用与异常初始化实现
 *
 * Bring-up mirrors the vendor sample `YTM_DRIVER_FPU_OPEN()` and
 * is implemented on top of the highest-level interfaces available
 * on this SDK:
 *   - CMSIS  NVIC_EnableIRQ()        (replaces raw NVIC->ISER write)
 *   - SDK    CIM_FPUIE_FP*IE_MASK    (replaces raw 0x3F magic)
 *   - SDK    IPC_CTRL_CLKEN_MASK     (replaces raw 0x201 magic)
 *   - ARM    SCB->CPACR              (no higher-level wrapper exists;
 *                                      same idiom used by
 *                                      platform/.../system_YTM32B1MD1.c
 *                                      and the vendor sample.  The
 *                                      CP10/CP11 bit-field offsets are
 *                                      exposed locally as FPU_CPACR_*
 *                                      in fpu.h since CMSIS does not
 *                                      provide them.)
 */
#include "fpu.h"
#include "sdk_project_config.h"  /* CIM, IPC, SCB via device_registers.h; NVIC via core_cm33.h */

#define LOG_NAME  "FPU"
#include "log.h"

/* Cumulative exception counter - incremented from the FPU ISR.
 * Read by Fpu_GetExceptionCount() for SOC / diag. */
static volatile u32 s_fpu_ex_count = 0u;

/**
 * @brief   Strong FPU exception ISR - replaces the weak default in
 *          board/vector_table_copy.c.  Stays short (no printf, no
 *          peripheral access); bumps a counter and returns so the
 *          next FP instruction can clear the sticky FPSCR flag.
 */
void FPU_IRQHandler(void)
{
    s_fpu_ex_count++;
}

u32 Fpu_GetExceptionCount(void)
{
    return s_fpu_ex_count;
}

c02b2_result_t Fpu_Init(void)
{
    /* 1. CP10/CP11 full access.  No higher-level wrapper exists in
     *    the SDK or CMSIS, so we write the SCB register directly.
     *    Idempotent (|=) so it is safe even if SystemInit() already
     *    set these bits under #ifdef ENABLE_FPU. */
    SCB->CPACR |= FPU_CPACR_CP10_FULL | FPU_CPACR_CP11_FULL;

    /* 2. CIM IPC routing - unlock CTRL[123] then enable.  The
     *    write-0-then-write-enable sequence matches the vendor
     *    sample and is required for the CIM to accept the lock.
     *    No SDK wrapper exists (the SDK drivers that touch IPC
     *    CTRL[] do so directly, e.g. adc_hw_access.h and
     *    clock_YTM32B1Mx.c); use the SDK's IPC_CTRL_*_MASK
     *    constants instead of magic numbers.
     *
     *    0x201 = IPC_CTRL_CLKEN (0x1) | IPC_CTRL_SRCSEL(2) (0x200)
     *    - CLKEN enables the IPC mux
     *    - SRCSEL=2 routes the FPU exception source to NVIC IRQ17
     */
    IPC->CTRL[123] = 0u;
    IPC->CTRL[123] = IPC_CTRL_CLKEN_MASK | IPC_CTRL_SRCSEL(2u);

    /* 3. NVIC enable for FPU_IRQn.  Use the CMSIS wrapper - it
     *    expands to NVIC->ISER write plus the necessary compiler
     *    barrier pair (matches the original __asm volatile("":::"memory")
     *    fence we had hand-rolled). */
    NVIC_EnableIRQ(FPU_IRQn);

    /* 4. Enable all six FPU exception sources (IOC / DZC / UFC /
     *    OFC / IDC / IXC).  0x3F = 0b0011_1111 covers bits 0..5,
     *    which is exactly the OR of the six CIM_FPUIE_FP*IE_MASK
     *    constants - using the named constants documents intent
     *    and survives register-layout changes. */
    CIM->FPUIE = (u32)CIM_FPUIE_FPIOCIE_MASK
               | (u32)CIM_FPUIE_FPDZCIE_MASK
               | (u32)CIM_FPUIE_FPUFCIE_MASK
               | (u32)CIM_FPUIE_FPOFCIE_MASK
               | (u32)CIM_FPUIE_FPIDCIE_MASK
               | (u32)CIM_FPUIE_FPIXCIE_MASK;

    LOG_I("FPU enabled (CP10/CP11 full, IPC[123]=CLKEN|SRCSEL(2), FPUIE=IOC|DZC|UFC|OFC|IDC|IXC)");
    return C02B2_OK;
}
