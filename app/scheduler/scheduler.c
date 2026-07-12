/**
 * @file    scheduler.c
 * @brief   Super-loop scheduler driven by a static module registry
 * @brief   由静态模块注册表驱动的主循环调度器
 *
 * @details Each module .c file calls SCHED_REGISTER(mod_xxx) which
 *          emits a __root const pointer to its mod_desc_t into the
 *          .sched_modules section. scheduler.c maintains a fixed
 *          registry table (g_sched_modules[]) referencing each
 *          module descriptor; ILINK/ld verifies that every referenced
 *          descriptor has a matching __root symbol (proving the
 *          SCHED_REGISTER macro was actually invoked).
 *
 *          Order = link order of the module .c files; scheduler.c
 *          does not reorder.
 */
#include "scheduler.h"
#include "rti.h"
#include "drv_api/rti_defer/rti_defer.h"
#if SCHED_BUDGET_EN
#include "osif.h"
#endif
/* Each module .c file defines `const mod_desc_t mod_xxx;`. Forward-declare
 * them here so the registry table below can take their addresses. The
 * `extern` declaration matches the definition emitted by SCHED_REGISTER
 * in the module .c. */
extern const mod_desc_t mod_template;
extern const mod_desc_t mod_can_rx;
extern const mod_desc_t mod_can_tx;
extern const mod_desc_t mod_can_demo;
extern const mod_desc_t mod_rti_demo;

#define LOG_NAME  "SCH"
#include "log.h"
/* REVIEW: C5 g_sched_modules[] + 5 extern declarations double source (Phase 3 switch to __sched_modules section) */
/* REVIEW: A6 Scheduler_Run has no reentry assert (Phase 1) */
/* REVIEW: A8 callback install timing contract undocumented (Phase 1 docs only) */
/* REVIEW: B2 prv_module_count recomputes every walk (Phase 2 micro) */

#if SCHED_BUDGET_EN
/* Cortex-M0+ has no DWT/CYCCNT. Use OSIF_GetMilliseconds() for
 * tick() duration measurement. Resolution = 1ms, so set
 * SCHED_BUDGET_US to at least 1000 to avoid noise. */

/**
 * @brief   Snapshot the current 1ms tick
 * @brief   采样当前 1ms tick
 */
static inline uint32_t prv_cycle_cnt(void)
{
    return OSIF_GetMilliseconds();
}

/**
 * @brief   Convert 1ms ticks to microseconds
 * @brief   1ms tick 转微秒
 */
static inline uint32_t prv_cycles_to_us(uint32_t ms)
{
    return ms * 1000u;
}
#endif

/**
 * @brief   Module registry
 * @brief   模块注册表
 *
 * @details Each module .c file calls SCHED_REGISTER(mod_xxx) which
 *          emits a __root const pointer to the module descriptor so
 *          the linker retains the symbol even though no C code reads
 *          it (i.e. prevents dead-code elimination). scheduler.c
 *          maintains a fixed-size pointer table (g_sched_modules[])
 *          referencing each descriptor by name; this is the source
 *          of truth for the boot / tick / standby walks.
 *
 *          Order = the order of the entries in g_sched_modules[]
 *          below (not link order of the .c files).
 *
 *          Two steps to add a new module:
 *            1. Add SCHED_REGISTER(mod_xxx); to the module .c
 *            2. Append &mod_xxx to g_sched_modules[] below.
 *          No edits to board/yt_linker.icf.
 */
static const mod_desc_t * const g_sched_modules[] = {
    &mod_template,
    &mod_can_rx,
    &mod_can_tx,
    &mod_can_demo,
    &mod_rti_demo,
};

static uint32_t prv_module_count(void)
{
    return (uint32_t)(sizeof(g_sched_modules) / sizeof(g_sched_modules[0]));
}

#define SCHED_MODS_PTRS (g_sched_modules)

/**
 * @brief   Initialize the scheduler and run mcu_init on every module
 * @brief   初始化调度器并依次调用所有模块的 mcu_init
 */
void Scheduler_Init(void)
{
    const uint32_t n = prv_module_count();
    LOG_I("init: %u modules", (unsigned)n);
    for (uint32_t i = 0; i < n; i++) {
        const mod_desc_t *m = SCHED_MODS_PTRS[i];
        LOG_I("  [%02u] %s", (unsigned)i, m->name);
        if (m->mcu_init) m->mcu_init(1u);
    }
}

/**
 * @brief   Walk the registry and call wakeup_init on every module
 * @brief   遍历注册表, 依次调用所有模块的 wakeup_init
 */
void Scheduler_WakeupInit(void)
{
    const uint32_t n = prv_module_count();
    for (uint32_t i = 0; i < n; i++) {
        const mod_desc_t *m = SCHED_MODS_PTRS[i];
        if (m->wakeup_init) m->wakeup_init();
    }
}

/**
 * @brief   Broadcast IGN ON event to every module
 * @brief   向所有模块广播 IGN ON 事件
 */
void Scheduler_OnIgnOn(void)
{
    const uint32_t n = prv_module_count();
    for (uint32_t i = 0; i < n; i++) {
        const mod_desc_t *m = SCHED_MODS_PTRS[i];
        if (m->on_ign_on) m->on_ign_on();
    }
}

/**
 * @brief   Run one super-loop tick over all modules
 * @brief   在所有模块上执行一次主循环 tick
 *
 * @details Fire any RTI_Defer callbacks whose deadline has passed
 *          BEFORE walking the module registry, so a callback can
 *          re-enter scheduler-visible state in the same tick.
 */
void Scheduler_Run(void)
{
    RTI_DeferTick();
    const uint32_t n = prv_module_count();
    for (uint32_t i = 0; i < n; i++) {
        const mod_desc_t *m = SCHED_MODS_PTRS[i];
        if (m->tick == NULL) { continue; }
#if SCHED_BUDGET_EN
        /* OSIF_GetMilliseconds is monotonic; elapsed = now - start. */
        const uint32_t start = prv_cycle_cnt();
        m->tick();
        const uint32_t dur_us = prv_cycles_to_us(prv_cycle_cnt() - start);
        m->_budget_last_dur_us = dur_us;
        if (dur_us > SCHED_BUDGET_US) {
            m->_budget_overrun_cnt++;
            LOG_W("[SCH] %s tick overrun: %u us (budget %u us, count=%u)",
                  m->name, (unsigned)dur_us, (unsigned)SCHED_BUDGET_US,
                  (unsigned)m->_budget_overrun_cnt);
        }
#else
        m->tick();
#endif
    }
}

/**
 * @brief   Broadcast standby event before entering low-power mode
 * @brief   进入低功耗模式前向所有模块广播 standby 事件
 */
void Scheduler_Standby(void)
{
    const uint32_t n = prv_module_count();
    for (uint32_t i = 0; i < n; i++) {
        const mod_desc_t *m = SCHED_MODS_PTRS[i];
        if (m->standby) m->standby();
    }
}
