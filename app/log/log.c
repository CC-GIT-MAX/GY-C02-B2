/**
 * @file    log.c
 * @brief   Log dispatcher implementation
 */
#include "log.h"
#include <stdarg.h>
#include <stdio.h>

static const char *const k_lvl_str[] = {
    [LOG_LVL_ERROR] = "E",
    [LOG_LVL_WARN]  = "W",
    [LOG_LVL_INFO]  = "I",
    [LOG_LVL_DEBUG] = "D",
};

void Log_Print(log_level_t lvl, const char *mod, const char *fmt, ...)
{
    if (lvl > LOG_LEVEL) {
        return;
    }
    LOG_PRINTF("[%s][%s] ", k_lvl_str[lvl], mod);
    va_list ap;
    va_start(ap, fmt);
    /* tiny printf supports vprintf style; use vprintf through a small bridge */
    {
        char buf[160];
        int n = vsnprintf(buf, sizeof(buf), fmt, ap);
        if (n > 0) {
            LOG_PRINTF("%s", buf);
        }
    }
    va_end(ap);
    LOG_PRINTF("\r\n");
}
