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
#ifndef LBX_LOG_H
#define LBX_LOG_H

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
  #include "printf.h"
  #define LOG_PRINTF(...)  printf(__VA_ARGS__)
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

#endif /* LBX_LOG_H */
