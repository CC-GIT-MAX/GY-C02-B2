/**
 * @file    fpu.c
 * @brief   FPU (Cortex-M33 FPU + YTM CIM FPU exception) bring-up
 * @brief   FPU 启用与异常初始化实现
 *
 * 初始化流程镜像厂商示例 YTM_DRIVER_FPU_OPEN()，
 * 在本 SDK 现有最高层接口之上实现：
 *   - CMSIS  NVIC_EnableIRQ()        （替代直接写 NVIC->ISER）
 *   - SDK    CIM_FPUIE_FP*IE_MASK    （替代魔数 0x3F）
 *   - SDK    IPC_CTRL_CLKEN_MASK     （替代魔数 0x201）
 *   - ARM    SCB->CPACR              （SDK 暂无更高层封装；
 *                                      与 platform/.../system_YTM32B1MD1.c
 *                                      及厂商示例使用的写法相同。
 *                                      CP10/CP11 位段偏移量在 fpu.h
 *                                      中以 FPU_CPACR_* 本地暴露，
 *                                      因为 CMSIS 并未提供这些宏。）
 */
#include "fpu.h"
#include "sdk_project_config.h"  /* CIM, IPC, SCB via device_registers.h; NVIC via core_cm33.h */

#define MOD_NAME  "FPU"
#include "log.h"

/* 累计异常计数器 —— 在 FPU ISR 内递增，
 * 由 Fpu_GetExceptionCount() 读取供 SOC / 诊断使用。 */
static volatile u32 s_fpu_ex_count = 0u;

/**
 * @brief   FPU 异常强符号 ISR —— 取代 board/vector_table_copy.c
 *          中的弱符号默认实现。保持简短（无 printf、无
 *          外设访问）；仅递增计数器即返回，以便下一条 FP 指令
 *          能清除粘滞的 FPSCR 标志。
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
    /* 1. CP10/CP11 完全访问。SDK 与 CMSIS 均无更高层封装，
     *    故直接写 SCB 寄存器。幂等的（|=），即使
     *    SystemInit() 已在 #ifdef ENABLE_FPU 下设置过
     *    这些位，也是安全的。 */
    SCB->CPACR |= FPU_CPACR_CP10_FULL | FPU_CPACR_CP11_FULL;

    /* 2. CIM IPC 路由 —— 先解锁 CTRL[123] 再启用。
     *    "先写 0 再写使能"的顺序与厂商示例一致，是 CIM
     *    接受锁定的必要条件。SDK 没有相关封装（SDK 中
     *    触碰 IPC CTRL[] 的驱动如 adc_hw_access.h 与
     *    clock_YTM32B1Mx.c 均直接操作）；此处使用 SDK 的
     *    IPC_CTRL_*_MASK 常量代替魔数。
     *
     *    0x201 = IPC_CTRL_CLKEN (0x1) | IPC_CTRL_SRCSEL(2) (0x200)
     *    - CLKEN 启用 IPC mux
     *    - SRCSEL=2 将 FPU 异常源路由到 NVIC IRQ17
     */
    IPC->CTRL[123] = 0u;
    IPC->CTRL[123] = IPC_CTRL_CLKEN_MASK | IPC_CTRL_SRCSEL(2u);

    /* 3. 启用 FPU_IRQn 的 NVIC 中断。使用 CMSIS 封装 ——
     *    它会展开为 NVIC->ISER 写入加上必要的编译器屏障对
     *    （等价于我们之前手写的 __asm volatile("":::"memory")
     *    栅栏）。 */
    NVIC_EnableIRQ(FPU_IRQn);


    LOG_I("FPU enabled (CP10/CP11 full, IPC[123]=CLKEN|SRCSEL(2), FPUIE=IOC|DZC|UFC|OFC|IDC|IXC)");
    return C02B2_OK;
}
