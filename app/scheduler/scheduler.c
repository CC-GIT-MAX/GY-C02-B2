/**
 * @file    scheduler.c
 * @brief   Super-loop scheduler driven by a static module registry
 * @brief   由静态模块注册表驱动的主循环调度器
 *
 * @details 每个模块 .c 文件调用 SCHED_REGISTER(mod_xxx)，该宏会向
 *          .sched_modules section 写入一条 __root const 指针指向
 *          该模块的 mod_desc_t。scheduler.c 维护一个固定注册表
 *          (g_sched_modules[]) 引用各描述符；ILINK/ld 校验每个被引用
 *          的描述符都有匹配的 __root 符号（证明 SCHED_REGISTER 确实被
 *          调用过）。
 *
 *          顺序 = 各模块 .c 文件的链接顺序，scheduler.c 不重排。 */
#include "scheduler.h"
#include "rti.h"
#include "drv_api/rti_defer/rti_defer.h"
#if SCHED_BUDGET_EN
#include "osif.h"
#endif
/* 每个模块 .c 文件定义 `const mod_desc_t mod_xxx;`。此处前向声明
 * 这些符号以便下面的注册表可以取它们的地址。`extern` 与各模块
 * .c 中 SCHED_REGISTER 产生的定义对应。*/
extern const mod_desc_t mod_template;
extern const mod_desc_t mod_can_rx;
extern const mod_desc_t mod_can_tx;
extern const mod_desc_t mod_can_demo;
extern const mod_desc_t mod_rti_demo;

#define MOD_NAME  "SCH"
#include "log.h"
/* REVIEW: C5 g_sched_modules[] + 5 个 extern 声明双重源 (Phase 3 改用 __sched_modules section) */
/* Phase 1 / A6: ack. Scheduler_Run 入口 s_sched_depth != 0 自旋 + WDG 重启守卫已在实现. Marker closed. */
/* Phase 1 / A8: ack. mod_desc_t tick/standby/install 字段的安装时序契约已在 docs/ARCHITECTURE.md 锁定 (初始化阶段同步安装, 运行期不可变更). 见 docs/REVIEW_NOTES.md. Marker closed. */
/* REVIEW: B2 prv_module_count 每次遍历都重算 (Phase 2 micro) */

#if SCHED_BUDGET_EN
/* Cortex-M0+ 没有 DWT/CYCCNT。用 OSIF_GetMilliseconds() 测
 * 量 tick() 时长。分辨率 = 1ms，故 SCHED_BUDGET_US 应至少设为
 * 1000 以避免噪声。*/

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
 * @details 每个模块 .c 文件调用 SCHED_REGISTER(mod_xxx)，该宏会
 *          向模块描述符写入一条 __root const 指针，以便链接器保留该
 *          符号（即使没有 C 代码读它——即防止 dead-code elimination）。
 *          scheduler.c 维护一张固定大小的指针表 (g_sched_modules[])，
 *          按名字引用各描述符；这就是 boot / tick / standby 遍历的
 *          单一真实源。
 *
 *          顺序 = g_sched_modules[] 中条目的顺序（与 .c 文件链接顺序无关）。
 *
 *          新增模块两步走：
 *            1. 在模块 .c 中加 SCHED_REGISTER(mod_xxx);
 *            2. 在下方 g_sched_modules[] 末尾追加 &mod_xxx。
 *          无需改 board/yt_linker.icf。 */
static const mod_desc_t * const g_sched_modules[] = {
    &mod_template,
    &mod_can_rx,
    &mod_can_tx,
    &mod_can_demo,
    &mod_rti_demo,
};

/* Phase 2 / B2：在文件作用域缓存模块数量。g_sched_modules[] 是
 * `static const`，大小不变；将其放在 `static const` 里（而非函数调用）
 * 让编译器在每个调用点折成 32-bit 立即数，省掉每次遍历中的
 * sizeof/除法分支。*/
static const uint32_t k_module_count = (uint32_t)(sizeof(g_sched_modules) / sizeof(g_sched_modules[0]));

/**
 * @brief   Scheduler reentry guard (Phase 1 / A6)
 *
 * @details 在 Scheduler_Run 入口自增，出口自减。模块 tick() 若
 *          回调进调度器（或 defer 回调这样做），就会触发该守卫。守卫
 *          进入死循环让看门狗复位 MCU——半截遍历比干净复位更糟糕。
 *
 * @note    裸机 / 主循环：守卫是普通 u8，不是计数信号量。
 *          ISR 上下文从不调用 Scheduler_Run。 */
static volatile uint8_t s_sched_depth = 0u;

#define SCHED_MODS_PTRS (g_sched_modules)

/**
 * @brief   Initialize the scheduler and run mcu_init on every module
 * @brief   初始化调度器并依次调用所有模块的 mcu_init
 */
void Scheduler_Init(void)
{
    const uint32_t n = k_module_count;
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
    const uint32_t n = k_module_count;
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
    const uint32_t n = k_module_count;
    for (uint32_t i = 0; i < n; i++) {
        const mod_desc_t *m = SCHED_MODS_PTRS[i];
        if (m->on_ign_on) m->on_ign_on();
    }
}

/**
 * @brief   Run one super-loop tick over all modules
 * @brief   在所有模块上执行一次主循环 tick
 *
 * @details 先触发所有 deadline 已到的 RTI_Defer 回调，再遍历模块
 *          注册表，以便回调能在同一 tick 内重新进入调度器可见状态。 */
void Scheduler_Run(void)
{
    /* Phase 1 / A6：重入守卫。*/
    if (s_sched_depth != 0u) {
        for (;;) { /* WDG will reset */ }
    }
    s_sched_depth = 1u;

    RTI_DeferTick();
    const uint32_t n = k_module_count;
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
    s_sched_depth = 0u;
}

/**
 * @brief   Broadcast standby event before entering low-power mode
 * @brief   进入低功耗模式前向所有模块广播 standby 事件
 */
void Scheduler_Standby(void)
{
    const uint32_t n = k_module_count;
    for (uint32_t i = 0; i < n; i++) {
        const mod_desc_t *m = SCHED_MODS_PTRS[i];
        if (m->standby) m->standby();
    }
}
