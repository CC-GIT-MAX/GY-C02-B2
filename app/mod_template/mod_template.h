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
 *   - Return lbx_result_t from every public API.
 *   - Own period control via RTI_IsElapsed(); no global flag variables.
 */
#ifndef LBX_MOD_TEMPLATE_H
#define LBX_MOD_TEMPLATE_H

#include "scheduler.h"
#include "result.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Public API ----------------------------------------------------------- */
lbx_result_t Template_SetDiagValue(uint32_t v);
uint32_t     Template_GetDiagValue(void);

/* Module descriptor (consumed by scheduler.c) -------------------------- */
extern const mod_desc_t mod_template;

#ifdef __cplusplus
}
#endif

#endif /* LBX_MOD_TEMPLATE_H */
