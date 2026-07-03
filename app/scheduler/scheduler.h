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
#ifndef LBX_SCHEDULER_H
#define LBX_SCHEDULER_H

#include <stdint.h>

struct mod_desc_s;

typedef struct mod_desc_s {
    const char *name;                       /* for log / debug */
    void (*init)(uint8_t cold_boot);
    void (*on_ign_on)(void);
    void (*tick)(void);
    void (*standby)(void);
} mod_desc_t;

void Scheduler_Init(void);
void Scheduler_OnIgnOn(void);
void Scheduler_Run(void);                   /* call in main super-loop */
void Scheduler_Standby(void);

#endif /* LBX_SCHEDULER_H */
