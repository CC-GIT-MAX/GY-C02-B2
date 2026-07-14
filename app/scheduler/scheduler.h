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
 * @details 按声明顺序遍历 g_sched_modules[]，对每个非 NULL 的
 *          mcu_init 钩子调用一次。引导时调用一次，位于任何依赖

 *          "模块已就绪" 状态的外设初始化之前。
 * @param   none
 * @return  void
 */
void Scheduler_Init(void);
/**
 * @brief   Run wakeup_init() on every registered module
 * @brief   遍历注册表, 依次调用每个模块的 wakeup_init
 *
 * @details 与 Scheduler_Init 分开，让模块能把冷启动相关的设置
 *          放在 mcu_init，把复位恢复相关的工作(NVIC 优先级复位、
 *          唤醒源重新使能)放在 wakeup_init。
 *          在 Scheduler_Init 与 Scheduler_OnIgnOn 之间调用一次。
 * @param   none
 * @return  void
 */
void Scheduler_WakeupInit(void);
/**
 * @brief   Broadcast on_ign_on() to every registered module
 * @brief   向所有模块广播 on_ign_on
 *
 * @details 按声明顺序遍历 g_sched_modules[]，对每个非 NULL 的
 *          on_ign_on 钩子调用一次。在 KL15 上电之后调用一次
 *          (冷启动时若 IGN 已接通则立即调用)。
 * @param   none
 * @return  void
 */
void Scheduler_OnIgnOn(void);
/**
 * @brief   One super-loop iteration
 * @brief   单次 super-loop 循环
 *
 * @details 按声明顺序遍历 g_sched_modules[]，对每个非 NULL 的
 *          tick 钩子调用一次。各模块通过 RTI slot API 各自决定子周期。
 *          由 main() 的 for(;;) 循环在 __WFI() 休眠窗口之间调用。
 * @param   none
 * @return  void
 */
void Scheduler_Run(void);
/**
 * @brief   Broadcast standby() to every registered module
 * @brief   向所有模块广播 standby
 *
 * @details MCU 进入低功耗模式前，按声明顺序遍历 g_sched_modules[]，
 *          对每个非 NULL 的 standby 钩子调用一次：模块在此释放外设
 *          并保存各自的周期上下文。
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
