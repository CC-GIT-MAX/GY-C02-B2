/**
 * @file    scheduler.c
 * @brief   Super-loop scheduler that walks the module registry
 */
#include "scheduler.h"
#include "rti.h"

#define LOG_NAME  "SCH"
#include "log.h"

/* Forward declarations of business module descriptors. */
extern const mod_desc_t mod_template;
extern const mod_desc_t mod_power;

const mod_desc_t * const g_modules[] = {
    /* Order is significant for log readability but not for correctness. */
    &mod_template,
    &mod_power,
    /* &mod_can, &mod_diag, ... append here */
};

static const uint32_t g_module_cnt = sizeof(g_modules) / sizeof(g_modules[0]);

void Scheduler_Init(void)
{
    LOG_I("init: %u modules", (unsigned)g_module_cnt);
    for (uint32_t i = 0; i < g_module_cnt; i++) {
        const mod_desc_t *m = g_modules[i];
        LOG_I("  [%02u] %s", (unsigned)i, m->name);
        if (m->init) m->init(1u);
    }
}

void Scheduler_OnIgnOn(void)
{
    for (uint32_t i = 0; i < g_module_cnt; i++) {
        const mod_desc_t *m = g_modules[i];
        if (m->on_ign_on) m->on_ign_on();
    }
}

void Scheduler_Run(void)
{
    for (uint32_t i = 0; i < g_module_cnt; i++) {
        const mod_desc_t *m = g_modules[i];
        if (m->tick) m->tick();
    }
}

void Scheduler_Standby(void)
{
    for (uint32_t i = 0; i < g_module_cnt; i++) {
        const mod_desc_t *m = g_modules[i];
        if (m->standby) m->standby();
    }
}
