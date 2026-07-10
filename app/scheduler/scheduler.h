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

void Scheduler_Init(void);
void Scheduler_WakeupInit(void);
void Scheduler_OnIgnOn(void);
void Scheduler_Run(void);
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
