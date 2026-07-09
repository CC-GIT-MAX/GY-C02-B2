/**
 * @file    scheduler.c
 * @brief   Super-loop scheduler that walks the module registry
 */
#include "scheduler.h"
#include "rti.h"
#include "drv_api/rti_defer/rti_defer.h"

#define LOG_NAME  "SCH"
#include "log.h"

/* Forward declarations of business module descriptors. */
extern const mod_desc_t mod_template;
extern const mod_desc_t mod_can_rx;
extern const mod_desc_t mod_can_tx;
extern const mod_desc_t mod_can_demo;

/**
 * @brief   Module registry (registration order = tick / init order)
 * @brief   模块注册表（注册顺序 = tick / init 顺序）
 *
 * @details The order is significant for two things:
 *   - Log readability (init order printed top-to-bottom)
 *   - Tick data flow: can_rx runs BEFORE can_tx so the RX
 *     dispatcher publishes fresh signals that can_tx can read.
 */
const mod_desc_t * const g_modules[] = {
    /* Order is significant for log readability but not for correctness. */
    &mod_template,
    &mod_can_rx,    /* pull from ring first */
    &mod_can_tx,    /* push to bus */
    &mod_can_demo,  /* bring-up demo: exercises Signal_Get + raw cache + TX */
    /* &mod_diag, &mod_storage, ... append here */
};

/* Compile-time count via sizeof trick. */
static const uint32_t g_module_cnt = sizeof(g_modules) / sizeof(g_modules[0]);

/**
 * @brief   Initialize the scheduler and run mcu_init on every module
 * @brief   初始化调度器并依次调用所有模块的 mcu_init
 *
 * @details First scheduler phase. Walks g_modules[] in registration
 *          order and calls each module's @c mcu_init(cold_boot=1)
 *          hook.  NULL hooks are silently skipped.  The matching
 *          wakeup phase is Scheduler_WakeupInit() - call it from
 *          main.c after Scheduler_Init() returns.
 */
void Scheduler_Init(void)
{
    LOG_I("init: %u modules", (unsigned)g_module_cnt);
    /* Walk the registry in order; log each name before mcu_init. */
    for (uint32_t i = 0; i < g_module_cnt; i++) {
        const mod_desc_t *m = g_modules[i];
        LOG_I("  [%02u] %s", (unsigned)i, m->name);
        /* mcu_init runs first - hardware power-on setup, RAM zero,
         * self-test. NULL hook is allowed (module opts out). */
        if (m->mcu_init) m->mcu_init(1u);
    }
}

/**
 * @brief   Walk the registry and call wakeup_init on every module
 * @brief   遍历注册表, 依次调用所有模块的 wakeup_init
 *
 * @details Second scheduler phase. Runs after Scheduler_Init (so all
 *          mcu_init hooks have completed) and before Scheduler_OnIgnOn
 *          (so KL15 / domain logic has not started yet). Use this
 *          phase to re-arm NVIC priorities, restore wake-source
 *          state, and prime caches that mcu_init left in a known
 *          reset state.
 *
 * @note    Not reentrant; call once per boot, between Scheduler_Init
 *          and Scheduler_OnIgnOn.
 */
void Scheduler_WakeupInit(void)
{
    for (uint32_t i = 0; i < g_module_cnt; i++) {
        const mod_desc_t *m = g_modules[i];
        /* wakeup_init is allowed to be NULL (module opts out). */
        if (m->wakeup_init) m->wakeup_init();
    }
}

/**
 * @brief   Broadcast IGN ON event to every module
 * @brief   向所有模块广播 IGN ON 事件
 */
void Scheduler_OnIgnOn(void)
{
    for (uint32_t i = 0; i < g_module_cnt; i++) {
        const mod_desc_t *m = g_modules[i];
        if (m->on_ign_on) m->on_ign_on();
    }
}

/**
 * @brief   Run one super-loop tick over all modules
 * @brief   在所有模块上执行一次主循环 tick
 */
void Scheduler_Run(void)
{
    /* Fire any RTI_Defer callbacks whose deadline has passed BEFORE
     * walking the module registry, so a callback can re-enter the
     * scheduler-visible state in the same tick. */
    RTI_DeferTick();
    for (uint32_t i = 0; i < g_module_cnt; i++) {
        const mod_desc_t *m = g_modules[i];
        if (m->tick) m->tick();
    }
}

/**
 * @brief   Broadcast standby event before entering low-power mode
 * @brief   进入低功耗模式前向所有模块广播 standby 事件
 */
void Scheduler_Standby(void)
{
    for (uint32_t i = 0; i < g_module_cnt; i++) {
        const mod_desc_t *m = g_modules[i];
        if (m->standby) m->standby();
    }
}
