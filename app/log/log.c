/**
 * @file    log.c
 * @brief   Log dispatcher implementation
 */
#include "log.h"
#include <stdarg.h>
#include <stdio.h>
/* REVIEW: B6 160B stack buf truncation has no marker (Phase 1 grow to 192B + ~ marker) */

/** @brief  Single-letter level tag table indexed by log_level_t. */
static const char *const k_lvl_str[] = {
    [LOG_LVL_ERROR] = "E",
    [LOG_LVL_WARN]  = "W",
    [LOG_LVL_INFO]  = "I",
    [LOG_LVL_DEBUG] = "D",
};

/**
 * @brief   Print a leveled log line
 * @brief   输出一条带等级与模块名的日志
 *
 * @details Format: "[LEVEL][MOD] <user-format>\\r\\n"
 *          Level filtering: log lines with lvl > LOG_LEVEL are
 *          dropped (compile-time filter is also applied by the
 *          LOG_* macros so they generate no code at all).
 *
 * @param[in]  lvl  Severity (LOG_LVL_*)
 * @param[in]  mod  Short module tag (e.g. "PWR", "CAN")
 * @param[in]  fmt  printf-style format string
 *
 * @note    Variable arguments follow `fmt` (printf semantics).
 */
void Log_Print(log_level_t lvl, const char *mod, const char *fmt, ...)
{
    /* Runtime filter: if compile-time filter missed, drop here. */
    if (lvl > LOG_LEVEL) {
        return;
    }
    /* Prefix: "[E][PWR] " etc. */
    LOG_PRINTF("[%s][%s] ", k_lvl_str[lvl], mod);
    va_list ap;
    va_start(ap, fmt);
    /* Format into a stack buffer to keep us printf-clean.
     * Phase 1 / B6: grew from 160 to 192 B (well within the ISR-friendly
     * stack budget of a Cortex-M33 with 8 KB main stack). vsnprintf
     * returns the FULL would-be length even when the destination is too
     * small, so detect truncation as (n >= sizeof(buf)) and append a
     * trailing "~" so a UART reader can tell the line was clipped. */
    {
        char buf[192];
        int n = vsnprintf(buf, sizeof(buf), fmt, ap);
        if (n > 0) {
            LOG_PRINTF("%s", buf);
            if (n >= (int)sizeof(buf)) {
                LOG_PRINTF("~");
            }
        }
    }
    va_end(ap);
    /* Newline: CRLF for terminal-friendly output. */
    LOG_PRINTF("\r\n");
}
