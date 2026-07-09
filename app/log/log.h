/**
 * @file    log.h
 * @brief   Lightweight leveled logging macros
 *
 * Usage in a .c file:
 *   #define MOD_NAME  "PWR"
 *   #include "log.h"
 *   LOG_I("bat=%u mv", mv);
 *   LOG_E("uv2 latch");
 *
 * The output is dispatched via Log_Print() which by default writes to
 * the project printf. Override by defining LOG_PRINTF to your own sink.
 */
#ifndef C02B2_LOG_H
#define C02B2_LOG_H

#include <stdint.h>
#include "result.h"

/**
 * @brief   Log severity levels
 * @brief   日志严重等级
 */
typedef enum {
    LOG_LVL_ERROR = 0,
    LOG_LVL_WARN  = 1,
    LOG_LVL_INFO  = 2,
    LOG_LVL_DEBUG = 3,
} log_level_t;

#ifndef LOG_LEVEL
  #define LOG_LEVEL  LOG_LVL_INFO
#endif

#ifndef MOD_NAME
  #define MOD_NAME  "APP"
#endif

#ifndef LOG_PRINTF
  /* Vendor printf.h defines `#define PRINTF printf_` and the tiny
   * printf_() implementation calls printf_char() which we wired
   * to LINFlexD UART2 in board/utility_print_config.c.
   * We deliberately use PRINTF (uppercase) instead of plain
   * printf - the bare printf() resolves to IAR DLib stdio
   * which routes through semihosting / stdout and never reaches
   * the UART. */
  #include "printf.h"
  #define LOG_PRINTF(...)  PRINTF(__VA_ARGS__)
#endif

/**
 * @brief   Print a leveled log line
 * @brief   输出一条带等级与模块名的日志
 *
 * @details Wraps the user format in `[LEVEL][MOD] ...` and writes
 *          through LOG_PRINTF. The level filter is also applied
 *          at compile time inside LOG_* macros, so calls with
 *          level > LOG_LEVEL become no-ops.
 *
 * @param[in]  lvl  Severity (LOG_LVL_*)
 * @param[in]  mod  Short module tag (e.g. "PWR", "CAN")
 * @param[in]  fmt  printf-style format string
 *
 * @note    Variable arguments follow `fmt` (printf semantics).
 *          The output ends with a CRLF.
 */
void Log_Print(log_level_t lvl, const char *mod, const char *fmt, ...);

#define LOG_E(...)  do { if (LOG_LEVEL >= LOG_LVL_ERROR) Log_Print(LOG_LVL_ERROR, MOD_NAME, __VA_ARGS__); } while(0)
#define LOG_W(...)  do { if (LOG_LEVEL >= LOG_LVL_WARN)  Log_Print(LOG_LVL_WARN,  MOD_NAME, __VA_ARGS__); } while(0)
#define LOG_I(...)  do { if (LOG_LEVEL >= LOG_LVL_INFO)  Log_Print(LOG_LVL_INFO,  MOD_NAME, __VA_ARGS__); } while(0)
#define LOG_D(...)  do { if (LOG_LEVEL >= LOG_LVL_DEBUG) Log_Print(LOG_LVL_DEBUG, MOD_NAME, __VA_ARGS__); } while(0)

/* ---------------------------------------------------------------- *
 *  Per-module 开发期调试日志宏 (compile-time switch)
 *
 *  默认全部 0,代码块完全不编译(包括 printf 参数评估),生产固件
 *  零开销。开发/联调时按需打开。打开方法:在该模块的 .c 文件
 *  顶部 #define 后再包含本头即可:
 *
 *    // app/foo/foo.c
 *    #define FOO_DEBUG_LOG  1
 *    #define LOG_NAME  "FOO"
 *    #include "log.h"
 *
 *  命名约定: <MODULE>_DEBUG_LOG (模块名大写,与 MOD_NAME 同步)。
 *
 *  适用范围:
 *    - 高频/逐 tick/逐帧的演示/跟踪类打印
 *    - 开发期临时插桩(后续功能稳定后应删除或保持关闭)
 *
 *  不适用范围(保持 LOG_I/LOG_W 无条件编译):
 *    - init / wakeup_init / on_ign_on / standby 生命周期标记
 *    - 错误告警 (TX full / RX error)
 *    - 自检/测试 PASS/FAIL 总结
 * ---------------------------------------------------------------- */
#ifndef CAN_DEBUG_LOG
  #define CAN_DEBUG_LOG  1   /* 临时打开验证, 验证后回退为 0 */
#endif

#ifndef CAN_DEMO_LOG
  #define CAN_DEMO_LOG  0   /* app/mod_can_demo/mod_can_demo.c 每个 tick 演示, 默认关闭 */
#endif

#ifndef SCHED_DEBUG_LOG
  #define SCHED_DEBUG_LOG  0  /* app/scheduler/scheduler.c */
#endif

#ifndef SIGNAL_TEST_LOG
  #define SIGNAL_TEST_LOG  0  /* app/signal/signal_test.c 用例详情 */
#endif

#ifndef STORAGE_DEBUG_LOG
  #define STORAGE_DEBUG_LOG  0  /* app/storage/ (all C files) */
#endif

#endif /* C02B2_LOG_H */
