/**
 * @file    scheduler.h
 * @brief   Module registry and super-loop scheduler
 *
 * Every business module implements a const mod_desc_t and is added to
 * the g_modules[] array in scheduler.c. The scheduler walks the array
 * and invokes the four lifecycle hooks:
 *
 *   init(cold_boot)   - called once at startup
 *   on_ign_on()       - called when KL15 transitions ON
 *   tick()            - called every super-loop iteration; modules
 *                       decide their own sub-period using RTI_IsElapsed()
 *   standby()         - called when entering low-power mode
 *
 * To add a new module:
 *   1. Implement the four hooks in your module .c
 *   2. Define `extern const mod_desc_t mod_xxx;` in its .h
 *   3. Add &mod_xxx to g_modules[] in scheduler.c
 *
 * No changes to main.c or scheduler.c body required.
 */
#ifndef C02B2_SCHEDULER_H
#define C02B2_SCHEDULER_H

#include <stdint.h>

struct mod_desc_s;

/**
 * @brief   Module lifecycle descriptor
 * @brief   模块生命周期描述符
 *
 * @details Each business module provides one of these as a `const`
 *          global symbol (e.g. `mod_can_rx`). The scheduler walks
 *          g_modules[] and invokes the four hooks in registration
 *          order on every super-loop iteration.
 *
 * @note    A NULL function pointer is allowed and silently skipped,
 *          so a module can opt out of a phase it does not need.
 */
typedef struct mod_desc_s {
    const char *name;                       /* for log / debug */
    void (*init)(uint8_t cold_boot);
    void (*on_ign_on)(void);
    void (*tick)(void);
    void (*standby)(void);
} mod_desc_t;

/**
 * @brief   Initialize the scheduler and call init() on every module
 * @brief   初始化调度器并依次调用所有模块的 init()
 *
 * @details Iterates g_modules[] in order, calls each module's
 *          init(cold_boot=1) hook. Must be called exactly once
 *          during system startup, after BSP_Init/DRV_Init and
 *          after the signal bus is up.
 *
 * @note    Not reentrant; call once before Scheduler_Run().
 */
void Scheduler_Init(void);

/**
 * @brief   Broadcast IGN ON event to every module
 * @brief   向所有模块广播 IGN ON 事件
 *
 * @details Calls on_ign_on() on every module. Invoked from main.c
 *          on cold boot if KL15 is already on, and from the IGN
 *          edge handler in mod_<name> on every rising edge.
 */
void Scheduler_OnIgnOn(void);

/**
 * @brief   Run one super-loop tick over all modules
 * @brief   在所有模块上执行一次主循环 tick
 *
 * @details Calls tick() on every module, in registration order.
 *          Designed to be called from an infinite loop in main()
 *          (the typical pattern is `for(;;) { Scheduler_Run(); __WFI(); }`).
 *          Each module uses RTI_IsElapsed() to time its sub-tasks.
 */
void Scheduler_Run(void);

/**
 * @brief   Broadcast standby event before entering low-power mode
 * @brief   进入低功耗模式前向所有模块广播 standby 事件
 *
 * @details Calls standby() on every module. Used by the power
 *          management flow to let modules quiesce (e.g. zero
 *          meter targets, suspend CAN TX) before the MCU sleeps.
 */
void Scheduler_Standby(void);

#endif /* C02B2_SCHEDULER_H */
