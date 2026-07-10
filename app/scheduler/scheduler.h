/**
 * @file    scheduler.h
 * @brief   Module registry and super-loop scheduler
 *
 * Each business module defines a `const mod_desc_t mod_xxx` at file
 * scope and calls SCHED_REGISTER(mod_xxx) in the same .c file.
 * scheduler.c maintains a fixed-size pointer table g_sched_modules[]
 * referencing every registered module descriptor; the table is the
 * source of truth for the lifecycle walks performed by Scheduler_Init
 * / WakeupInit / OnIgnOn / Run / Standby.
 *
 * Lifecycle hooks (all optional, NULL is silently skipped):
 *   mcu_init(cold_boot)  Called once during Scheduler_Init. cold_boot=1
 *                        for power-on reset. Use for clocks, RAM zero,
 *                        hardware self-test. Runs before any peripheral
 *                        or wakeup logic.
 *   wakeup_init()        Called once during Scheduler_WakeupInit (a
 *                        separate phase invoked from main.c between
 *                        Scheduler_Init and Scheduler_OnIgnOn). Use to
 *                        restore NVIC priorities and re-arm wake sources.
 *   on_ign_on()          Called from Scheduler_OnIgnOn when KL15 turns ON.
 *   tick()               Called every iteration of the main super-loop
 *                        (Scheduler_Run). Modules decide their own
 *                        sub-period using RTI slot API.
 *   standby()            Called from Scheduler_Standby before low-power.
 *
 * To add a new module:
 *   1. Implement the five hooks in the module .c
 *   2. Call SCHED_REGISTER(mod_xxx); at file scope
 *   3. Add `&mod_xxx` to g_sched_modules[] in scheduler.c
 *   4. Add `extern const mod_desc_t mod_xxx;` to scheduler.c
 */
#ifndef C02B2_SCHEDULER_H
#define C02B2_SCHEDULER_H

#include <stdint.h>

struct mod_desc_s;

typedef struct mod_desc_s {
    const char *name;
    void (*mcu_init)(uint8_t cold_boot);
    void (*wakeup_init)(void);
    void (*on_ign_on)(void);
    void (*tick)(void);
    void (*standby)(void);
} mod_desc_t;

/**
 * @brief   Run mcu_init() on every registered module
 * @brief   遍历注册表, 依次调用每个模块的 mcu_init
 *
 * @details Iterates g_sched_modules[] in declaration order and
 *          invokes each non-NULL mcu_init hook once. Called once
 *          during boot, before any peripheral init that depends
 *          on a module being ready.
 *
 * @param   none
 * @return  void
 */
void Scheduler_Init(void);
/**
 * @brief   Run wakeup_init() on every registered module
 * @brief   遍历注册表, 依次调用每个模块的 wakeup_init
 *
 * @details Separate from Scheduler_Init so modules can keep
 *          cold-boot setup in mcu_init and resume-from-reset
 *          work (NVIC priority restore, wake source re-arm) in
 *          wakeup_init. Called once between Scheduler_Init and
 *          Scheduler_OnIgnOn.
 *
 * @param   none
 * @return  void
 */
void Scheduler_WakeupInit(void);
/**
 * @brief   Broadcast on_ign_on() to every registered module
 * @brief   向所有模块广播 on_ign_on
 *
 * @details Iterates g_sched_modules[] in declaration order and
 *          invokes each non-NULL on_ign_on hook. Called once
 *          after KL15 turns ON (or immediately on cold boot if
 *          IGN is already on).
 *
 * @param   none
 * @return  void
 */
void Scheduler_OnIgnOn(void);
/**
 * @brief   One super-loop iteration
 * @brief   单次 super-loop 循环
 *
 * @details Iterates g_sched_modules[] in declaration order and
 *          invokes each non-NULL tick hook. Modules decide their
 *          own sub-period via the RTI slot API. Called from the
 *          main() for(;;) loop, between __WFI() sleep windows.
 *
 * @param   none
 * @return  void
 */
void Scheduler_Run(void);
/**
 * @brief   Broadcast standby() to every registered module
 * @brief   向所有模块广播 standby
 *
 * @details Iterates g_sched_modules[] in declaration order and
 *          invokes each non-NULL standby hook before the MCU
 *          enters low-power mode. Modules release peripherals
 *          and save any per-cycle context here.
 *
 * @param   none
 * @return  void
 */
void Scheduler_Standby(void);

#if defined(__IAR_SYSTEMS_ICC__)
  #define SCHED_REGISTER(_mod)                                            \
      __root static const mod_desc_t * const _sched_ref_##_mod = &(_mod)
#else
  #define SCHED_REGISTER(_mod)                                            \
      __attribute__((used))                                               \
      static const mod_desc_t * const _sched_ref_##_mod = &(_mod)
#endif

#endif /* C02B2_SCHEDULER_H */
