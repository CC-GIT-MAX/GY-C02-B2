/**
 * @file    mod_template.h
 * @brief   Business module skeleton template
 *
 * Copy this directory (rename to <feature>_mod) and implement the
 * four hooks. Then add &mod_<feature> to g_modules[] in scheduler.c.
 *
 * Conventions enforced:
 *   - All module state is `static` (no extern globals).
 *   - Cross-module data flows through Signal_* (see signal.h).
 *   - Return c02b2_result_t from every public API.
 *   - Own period control via RTI_IsElapsed(); no global flag variables.
 */
#ifndef C02B2_MOD_TEMPLATE_H
#define C02B2_MOD_TEMPLATE_H

#include "scheduler.h"
#include "result.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Public API ----------------------------------------------------------- */
/**
 * @brief   Set a diag value (for unit-test / manual injection)
 * @brief   设置一个诊断值（用于单元测试 / 手动注入）
 *
 * @param[in]  v  Value to store
 *
 * @return  c02b2_result_t
 * @retval  C02B2_OK            Stored
 * @retval  C02B2_ERR_NOT_READY Module not yet initialized
 */
c02b2_result_t Template_SetDiagValue(uint32_t v);

/**
 * @brief   Get the previously-set diag value
 * @brief   获取最近一次设置的诊断值
 *
 * @return  uint32_t  Last value, or 0 if never set
 */
uint32_t     Template_GetDiagValue(void);

/* Module descriptor (consumed by scheduler.c) -------------------------- */
/**
 * @brief   Module descriptor for mod_template (registered in scheduler.c)
 * @brief   mod_template 的模块描述符（在 scheduler.c 中注册）
 */
extern const mod_desc_t mod_template;

#ifdef __cplusplus
}
#endif

#endif /* C02B2_MOD_TEMPLATE_H */
