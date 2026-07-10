/**
 * @file    mod_rti_demo.c
 * @brief   Demo module proving RTI caller-private slots do not collide
 *
 * Registers two private slots, both bound to RTI_10MS, and
 * increments two separate counters. The tick prints both counters
 * every 1 s. Each slot fires ~100x per second (1000/10) but
 * counter A and counter B run on independent timelines.
 *
 * If RTI_OpenSlot returned a shared slot, only one of the two
 * RTI_SlotElapsed calls would ever return true per 100 ms window
 * (the later call would re-stamp and short-circuit the earlier).
 * With the new API both fire reliably.
 */
#include "mod_rti_demo.h"
#include "rti.h"

#define LOG_NAME  "RTID"
#include "log.h"

/* Compile-time switch: MOD_RTI_DEMO_EN
 *   0 (default) - module descriptor registered with NULL hooks, no
 *                 RTI slot allocated, no tick output. The module
 *                 becomes a "presence-only" entry in
 *                 scheduler.c::g_sched_modules[].
 *   1           - full demo: two RTI_10MS slots, counter print
 *                 every 1 s. Use for RTI slot / Scheduler
 *                 bring-up verification.
 *
 * Override on the compiler command line:
 *   iarbuild ... --define MOD_RTI_DEMO_EN=1
 *   gcc -DMOD_RTI_DEMO_EN=1 ...
 */
#ifndef MOD_RTI_DEMO_EN
  #define MOD_RTI_DEMO_EN  0
#endif

#if MOD_RTI_DEMO_EN
/* Two private slots, both 10ms. Both fire on the same 10 ms
 * tick so a and b stay in lockstep (a==b every log line), which
 * is the proof that the slots are independent - each slot keeps
 * its own last_ms and is evaluated independently. */
static rti_slot_t s_slot_a;
static rti_slot_t s_slot_b;

/* Independent counters prove independent timelines. */
static struct {
    uint32_t cnt_a;       /**< slot A fire count */
    uint32_t cnt_b;       /**< slot B fire count */
    uint32_t tick_1s_ms;  /**< last 1s tick for log cadence */
    uint32_t last_a;      /**< ms timestamp of last slot A fire */
    uint32_t last_b;      /**< ms timestamp of last slot B fire */
} s_demo;
#endif /* MOD_RTI_DEMO_EN */

static void prv_mcu_init(uint8_t cold_boot)
{
#if MOD_RTI_DEMO_EN
    (void)cold_boot;
    s_slot_a = RTI_OpenSlot(RTI_10MS);
    s_slot_b = RTI_OpenSlot(RTI_10MS);
    s_demo.cnt_a      = 0u;
    s_demo.cnt_b      = 0u;
    s_demo.tick_1s_ms = 0u;
    s_demo.last_a     = 0u;
    s_demo.last_b     = 0u;
    LOG_I("init (cold_boot=%u, slots opened)", (unsigned)cold_boot);
#else
    (void)cold_boot;
    LOG_I("init (cold_boot=%u, DISABLED: MOD_RTI_DEMO_EN=0)", (unsigned)cold_boot);
#endif
}

static void prv_wakeup_init(void)
{
#if MOD_RTI_DEMO_EN
    LOG_I("wakeup_init");
#endif
}

static void prv_on_ign_on(void)
{
#if MOD_RTI_DEMO_EN
    LOG_I("on_ign_on");
#endif
}

static void prv_tick(void)
{
#if MOD_RTI_DEMO_EN
    /* Each slot fires independently at 100ms cadence. */
    if (RTI_SlotElapsed(&s_slot_a)) {
        s_demo.cnt_a++;
        s_demo.last_a = RTI_GetTick1ms();
    }
    if (RTI_SlotElapsed(&s_slot_b)) {
        s_demo.cnt_b++;
        s_demo.last_b = RTI_GetTick1ms();
    }

    /* Log every 1s so the UART stays readable. */
    const uint32_t now = RTI_GetTick1ms();
    if ((now - s_demo.tick_1s_ms) < 1000u) { return; }
    s_demo.tick_1s_ms = now;

    LOG_I("a=%u b=%u (last_a=%u last_b=%u)",
          (unsigned)s_demo.cnt_a, (unsigned)s_demo.cnt_b,
          (unsigned)s_demo.last_a, (unsigned)s_demo.last_b);
#endif
}

static void prv_standby(void)
{
#if MOD_RTI_DEMO_EN
    LOG_I("standby");
#endif
}

const mod_desc_t mod_rti_demo = {
    .name      = "rti_demo",
    .mcu_init   = prv_mcu_init,
    .wakeup_init = prv_wakeup_init,
    .on_ign_on = prv_on_ign_on,
    .tick      = prv_tick,
    .standby   = prv_standby,
};

SCHED_REGISTER(mod_rti_demo);
