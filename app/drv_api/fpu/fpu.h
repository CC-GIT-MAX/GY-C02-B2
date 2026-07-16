/**
 * @file    fpu.h
 * @brief   FPU (Cortex-M33 FPU + YTM CIM FPU exception) bring-up API
 * @brief   FPU 启用与异常初始化接口
 *
 * 在本芯片上使用 FPU 需要完成三件事：
 *   1. SCB->CPACR 启用 CP10/CP11 完全访问（让 CPU 能真正
 *      发出 FP 指令）。SDK 已在 SystemInit() 的
 *      #ifdef ENABLE_FPU 段完成此操作；Fpu_Init()
 *      再次应用（幂等的 |=），因此即使构建未启用
 *      ENABLE_FPU 也是安全的。
 *   2. 解锁 IPC CTRL[123] 并设置 bit 0（CIM IPC 源路由，
 *      用于 FPU 异常）。若不进行此步，FPU 异常会
 *      产生但永远到不了 NVIC。
 *   3. NVIC ISER bit 17（FPU_IRQn）+ CIM->FPUIE = 0x3F，
 *      启用全部 6 个 FPU 异常（IOC / DZC / UFC / OFC / IDC / IXC）。
 *      app/drv_api/fpu/fpu.c 中的强符号 FPU_IRQHandler
 *      取代了 board/vector_table_copy.c 中的弱符号桩函数；
 *      它递增计数器，可通过 Fpu_GetExceptionCount() 读取
 *      （ISR 内不做日志输出）。
 */
#ifndef C02B2_DRV_API_FPU_H
#define C02B2_DRV_API_FPU_H

#include "types.h"
#include "result.h"

/* SCB->CPACR 位段定义（ARMv8-M 架构参考手册 B1.4.4）。
 *   CP10 占 bits [23:22]，CP11 占 bits [21:20]。
 *   每对位编码 0b11 表示完全访问（特权 + 非特权）。
 *   CMSIS 未提供 CPACR 的位段宏，因此在此本地定义，
 *   以消除 fpu.c 中的魔数 20 / 22。 */
#define FPU_CPACR_CP10_FULL  ((u32)3u << 20)  /**< CP10 full access (FPU ops)   */
#define FPU_CPACR_CP11_FULL  ((u32)3u << 22)  /**< CP11 full access (FPU regs)  */

/**
 * @brief   Enable the FPU coprocessor and route FPU exceptions to NVIC
 * @brief   启用 FPU 协处理器并把 FPU 异常路由到 NVIC
 *
 * @details 本函数幂等。CPACR 通常已由 SystemInit() 在
 *          ENABLE_FPU 下启用；本例程再次应用（|=，
 *          不会破坏其它位），以兼容跳过 ENABLE_FPU 的构建。
 *
 *          设置 IPC CTRL[123] 解锁并启用、NVIC ISER bit 17
 *          （FPU_IRQn），并将 CIM->FPUIE 设为 0x3F（全部 6 个
 *          异常源）。FPU 中断处理函数为 fpu.c 中定义的强符号，
 *          它对异常进行计数（ISR 内不做日志输出）。
 *
 *          在任何模块执行 FP 运算之前，由 DRV_Init() 调用一次。
 *
 * @return  c02b2_result_t    C02B2_OK: Always (FPU is hardware-managed, no failure mode)
 */
c02b2_result_t Fpu_Init(void);

/**
 * @brief   Cumulative count of FPU exceptions raised since boot
 * @brief   自启动以来 FPU 异常的累计计数
 *
 * @details 每个 FPU 异常在 FPU IRQ 处理函数内递增此计数器。
 *          便于 SOC / 诊断在不接 LOG 的情况下
 *          检测静默的 FP 故障。
 *
 * @return  u32  Total exception count
 */
u32 Fpu_GetExceptionCount(void);

#endif /* C02B2_DRV_API_FPU_H */
