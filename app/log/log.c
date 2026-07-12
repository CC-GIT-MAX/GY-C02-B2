/**
 * @file    log.c
 * @brief   Log dispatcher implementation
 */
#include "log.h"
#include <stdarg.h>
#include <stdio.h>
/* REVIEW: B6 160B 栈缓冲截断无标记（Phase 1 扩展至 192B + ~ 标记） */

/** @brief  按 log_level_t 索引的单字母级别标签表。 */
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
 * @details 格式："[LEVEL][MOD] <user-format>\r\n"
 *          级别过滤：lvl > LOG_LEVEL 的日志会被丢弃
 *          （LOG_* 宏在编译期也做了过滤，对应调用
 *          不会生成任何代码）。
 *
 * @param[in]  lvl  Severity (LOG_LVL_*)
 * @param[in]  mod  Short module tag (e.g. "PWR", "CAN")
 * @param[in]  fmt  printf-style format string
 *
 * @note    Variable arguments follow `fmt` (printf semantics).
 */
void Log_Print(log_level_t lvl, const char *mod, const char *fmt, ...)
{
    /* 运行期过滤：若编译期过滤未命中，此处丢弃。 */
    if (lvl > LOG_LEVEL) {
        return;
    }
    /* 前缀：如 "[E][PWR] "。 */
    LOG_PRINTF("[%s][%s] ", k_lvl_str[lvl], mod);
    va_list ap;
    va_start(ap, fmt);
    /* 格式化到栈缓冲以保持 printf 纯净。
     * Phase 1 / B6：缓冲由 160 增至 192 B（在 Cortex-M33
     * 8 KB 主栈预算内对 ISR 友好）。vsnprintf 即使目标缓冲过小，
     * 也会返回完整所需长度，因此通过 (n >= sizeof(buf))
     * 检测截断，并在末尾追加 "~" 让 UART 读取方知道该行被截断。 */
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
    /* 换行：CRLF 以兼容终端输出。 */
    LOG_PRINTF("\r\n");
}
