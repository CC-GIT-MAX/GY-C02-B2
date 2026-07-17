/**
 * @file    drv_init.c
 * @brief   Top-level peripheral driver init dispatcher
 *
 * After the refactor, this file no longer touches vendor SDK drivers
 * directly - it only calls the per-peripheral init helpers exposed
 * by app/drv_api/<periph>/<periph>_init.c. Each helper pulls its own
 * config from board/<periph>_config.c, so this file stays a flat list
 * of init calls and the LOG_I marker.
 *
 * The order is preserved verbatim from the pre-refactor file because
 * some drivers have soft ordering constraints (UTILITY_PRINT before
 * any LOG, LPTMR before RTI, etc.). When you add a new driver, append
 * its helper here and document any ordering dependency in the helper.
 */
#include "drv_init.h"

#include "sdk_project_config.h"

/* 每外设 init helper —— 每个对应一个 app/drv_api/<periph>/。
 * 在 drv_api/<periph>/<periph>.h 中声明，在 drv_api/<periph>/<periph>.c
 * 中定义（旧的 <periph>_init.c 已合并进这些 .c/.h 配对）。*/

/* 当前构建未用到的驱动也保留在此列表里，
 * 新增模块只需修改本分发器即可。*/

#include "drv_api/lptmr/lptmr.h"        /* Lptmr_Init (1 kHz RTI tick source)  */
#include "drv_api/adc/adc.h"            /* Adc_Init (converter config)         */
#include "drv_api/etmr/etmr.h"          /* Etmr_Init (counter + PWM channels)  */
#include "drv_api/i2c/i2c.h"            /* I2c_Init (master mode)              */
#include "drv_api/uart/uart.h"          /* Uart_Init (LINFlexD UART0)          */
#include "drv_api/uart/uart_parse.h"    /* Uart_Parse_Init (RX byte handler)   */
#include "drv_api/can/can_if.h"         /* Can_Init (FlexCAN1 + FlexCAN2)      */
#include "drv_api/fpu/fpu.h"            /* Fpu_Init (CPACR + IPC + FPUIE)      */
#include "drv_api/flash/flash.h"        /* Flash_Init (used by app/storage/kv) */

#define MOD_NAME  "DRV"
#include "log.h"
/* Phase 4 / C10: 决议保留现状。BSP_Init 末尾启用 WDG 是有意的早期 hang 保护 —— 捕获 BSP / DRV_Init 阶段死循环（flash_init 等待、I2C 总线卡死等），RTI 启动延迟期间的窗口也由 WDG 兜底。WDG 喂狗窗口（SDK 配置见 board/wdg_config.c）宽到覆盖最坏 RTI 启动路径。若未来需要严格"先 RTI 后 WDG"，把 Wdg_Init() 移到 main.c 中 RTI_Init() 之后即可。Marker closed. */
/* Phase 4 / A2: CanIf_Init 时序契约脆弱 — 见 can_if.c CanIf_Init() @note 段。可重复调用 / s_if_inited flag. Marker closed. */

/**
 * @brief   Initialize every peripheral driver
 * @brief   初始化全部外设驱动
 *
 * @return  c02b2_result_t  Always C02B2_OK (vendor errors are logged
 *                          inside each per-peripheral helper).
 */
c02b2_result_t DRV_Init(void)
{
    /* UTILITY_PRINT_Init() 故意不在这里调用 —— 它在 main.c 中紧跟
     * BSP_Init 之后调用，这样下面第一条 LOG_* 标记就能从最开始可见。*/
    /* 各外设 init —— 顺序对各项 helper 文件头中标注的依赖关系敏感。*/
    Fpu_Init();
    Lptmr_Init();
    Adc_Init();
    Etmr_Init();
    I2c_Init();
    Uart_Init();
    /* 解析模块必须晚于 Uart_Init —— 它要向 UART 注册逐字节 RX 回调 */
    (void)Uart_Parse_Init();
    Can_Init();
    Flash_Init();
    LOG_I("DRV init OK");
    return C02B2_OK;
}
