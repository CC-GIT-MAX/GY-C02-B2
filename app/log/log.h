/**
 * @file    log.h
 * @brief   Lightweight leveled logging macros
 *
 * 在 .c 文件中的用法：
 *   #define MOD_NAME  "PWR"
 *   #include "log.h"
 *   LOG_I("bat=%u mv", mv);
 *   LOG_E("uv2 latch");
 *
 * 输出经 Log_Print() 分发，默认写入工程 printf。
 * 可通过自行定义 LOG_PRINTF 指向自定义 sink 来覆盖。
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

/* MOD_NAME is the canonical module tag. The legacy LOG_NAME alias
 * is accepted for backward compatibility but only honored when
 * MOD_NAME is not defined by the including translation unit. */
#ifndef MOD_NAME
  #ifdef LOG_NAME
    #define MOD_NAME  LOG_NAME
  #else
    #define MOD_NAME  "APP"
  #endif
#endif

#ifndef LOG_PRINTF
  /* 厂商 printf.h 定义 `#define PRINTF printf_`，微型
   * printf_() 实现会调用 printf_char()，我们在
   * board/utility_print_config.c 中将其接到 LINFlexD UART2。
   * 故此处刻意使用 PRINTF（大写）而非普通 printf ——
   * 裸 printf() 会解析到 IAR DLib stdio，
   * 经 semihosting / stdout 路由，永远到不了 UART。 */
  #include "printf.h"
  #define LOG_PRINTF(...)  PRINTF(__VA_ARGS__)
#endif

/**
 * @brief   Print a leveled log line
 * @brief   输出一条带等级与模块名的日志
 *
 * @details 将用户格式包裹为 `[LEVEL][MOD] ...` 并通过 LOG_PRINTF
 *          输出。级别过滤在 LOG_* 宏的编译期也会执行，
 *          因此 level > LOG_LEVEL 的调用会变为 no-op。
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

#ifndef STORAGE_DEBUG_LOG
  #define STORAGE_DEBUG_LOG  0  /* app/storage/ (all C files) */
#endif

#endif /* C02B2_LOG_H */
