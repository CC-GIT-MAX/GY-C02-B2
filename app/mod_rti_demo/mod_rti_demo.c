/**
 * @file    mod_rti_demo.c
 * @brief   Demo module proving RTI caller-private slots do not collide
 *
 * 注册两个私有 slot，都绑定到 RTI_10MS，分别递增两个独立
 * 计数器。tick 每 1 s 打印两个计数器。每个 slot 每秒
 * 触发约 100 次（1000/10），但计数器 A 与计数器 B 各自
 * 沿独立时间线运行。
 *
 * 若 RTI_OpenSlot 返回共享 slot，则每个 100 ms 窗口内只有
 * 后调用的 RTI_SlotElapsed 会返回 true（后调用会重新打时间戳
 * 并短路先前的调用）。使用新 API 后两个 slot 都能可靠触发。
 */
#include "mod_rti_demo.h"
#include "rti.h"

#define MOD_NAME  "RTID"
#include "log.h"

/* 编译期开关：MOD_RTI_DEMO_EN
 *   0（默认） —— 模块描述符以 NULL 钩子注册，不分配 RTI slot，
 *                无 tick 输出。该模块成为 scheduler.c::g_sched_modules[]
 *                中的"仅占位"条目。
 *   1          —— 完整 demo：两个 RTI_10MS slot，每 1 s 打印计数器。
 *                用于 RTI slot / Scheduler 启动验证。
 *
 * 编译器命令行覆盖：
 *   iarbuild ... --define MOD_RTI_DEMO_EN=1
 *   gcc -DMOD_RTI_DEMO_EN=1 ...
 */
#ifndef MOD_RTI_DEMO_EN
  #define MOD_RTI_DEMO_EN  0
#endif

#if MOD_RTI_DEMO_EN
/* 两个私有 slot，均为 10ms。两者在同一 10 ms tick 上触发，
 * 因此 a 与 b 保持锁步（每行日志 a==b），这是 slot 相互独立的
 * 证明 —— 每个 slot 各自维护 last_ms 并独立求值。 */
static rti_slot_t s_slot_a;
static rti_slot_t s_slot_b;

/* 独立计数器证明独立时间线。 */
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
    /* 每个 slot 以 100ms 周期独立触发。 */
    if (RTI_SlotElapsed(&s_slot_a)) {
        s_demo.cnt_a++;
        s_demo.last_a = RTI_GetTick1ms();
    }
    if (RTI_SlotElapsed(&s_slot_b)) {
        s_demo.cnt_b++;
        s_demo.last_b = RTI_GetTick1ms();
    }

    /* 每 1 s 记一次日志，保持 UART 可读。 */
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
