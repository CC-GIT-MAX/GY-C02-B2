# Scheduler 模块使用指南

> 状态：**权威**（所有业务模块开发必须符合本文档）
> 创建：2026-07-13（Phase 5）
> 配合阅读：docs/SCHEDULER_ISSUES.md、docs/RTI_GUIDE.md、app/scheduler/scheduler.h

## 0. 设计目标

1. **裸机 + super-loop**：不依赖 RTOS，单线程 + IRQ
2. **模块自治**：每个业务模块自己决定 tick 子周期，通过 RTI slot API 订阅
3. **零动态注册**：所有模块在编译期固定，链接期通过 `__root` 符号校验
4. **冷启动确定性**：5 个 hook 调用顺序固定，无 malloc
5. **可重入保护**：`Scheduler_Run()` 入口自旋守卫，违规时 WDG 复位

---

## 1. 模块生命周期（5 个 Hook）

| Hook | 调用时机 | 用途 | 是否阻塞 |
|---|---|---|---|
| `mcu_init(cold_boot)` | `Scheduler_Init()` 内，逐模块 | 时钟、RAM 清零、硬件自检 | 短暂 |
| `wakeup_init()` | `Scheduler_WakeupInit()` 内，逐模块 | NVIC 优先级、唤醒源重使能 | 短暂 |
| `on_ign_on()` | `Scheduler_OnIgnOn()` 内，逐模块 | IGN=1 时业务初始化（CAN 启动等） | 中等 |
| `tick()` | `Scheduler_Run()` 主循环每轮 | 业务逻辑主体；可用 RTI slot API 拆子周期 | 必须短（<500us 典型） |
| `standby()` | `Scheduler_Standby()` 内，逐模块 | 进低功耗前的清理 | 短暂 |

**注册结构体**（`mod_desc_t`）：
```c
typedef struct mod_desc_s {
    const char *name;             /* 模块名（log 前缀）*/
    void (*mcu_init)(uint8_t cold_boot);
    void (*wakeup_init)(void);
    void (*on_ign_on)(void);
    void (*tick)(void);           /* 必须保证短路径 */
    void (*standby)(void);
} mod_desc_t;
```

任意 hook 可为 `NULL`，调用时静默跳过。

---

## 2. 注册流程（3 步新增模块）

### 步骤 1：模块 .c 内实现 + 注册

```c
/* app/my_mod.c */
#include "scheduler.h"

static void my_mcu_init(uint8_t cold_boot) { /* ... */ }
static void my_wakeup_init(void)           { /* ... */ }
static void my_on_ign_on(void)             { /* ... */ }
static void my_tick(void)                  { /* ... */ }
static void my_standby(void)               { /* ... */ }

const mod_desc_t mod_my = {
    .name        = "MY",
    .mcu_init    = my_mcu_init,
    .wakeup_init = my_wakeup_init,
    .on_ign_on   = my_on_ign_on,
    .tick        = my_tick,
    .standby     = my_standby,
};

SCHED_REGISTER(mod_my);   /* 宏：emit __root const pointer to .sched_modules */
```

### 步骤 2：在 `scheduler.c` 加前向声明

```c
extern const mod_desc_t mod_my;
```

### 步骤 3：在 `g_sched_modules[]` 末尾追加

```c
static const mod_desc_t * const g_sched_modules[] = {
    &mod_template,
    &mod_can_rx,
    &mod_can_tx,
    &mod_can_demo,
    &mod_rti_demo,
    &mod_my,           /* 新模块 */
};
```

**ILINK 校验**：步骤 1 的 `__root` 符号必须被步骤 3 的 `&mod_my` 引用，否则链接报 undefined reference——证明 SCHED_REGISTER 真的被调用过。

---

## 3. tick 子周期（RTI slot API）

`Scheduler_Run()` 每轮 ~1ms（OSIF_GetMilliseconds 分辨率）。模块 tick() 想做 5ms / 10ms / 100ms 任务，用 RTI slot API：

```c
#include "rti.h"

static void my_tick(void)
{
    if (RTI_SlotElapsed(5)) {       /* 每 5ms */
        /* 5ms 任务 */
    }
    if (RTI_SlotElapsed(10)) {      /* 每 10ms */
        /* 10ms 任务 */
    }
    if (RTI_SlotElapsed(100)) {     /* 每 100ms */
        /* 100ms 任务 */
    }
}
```

**并发限制**：RTI 内部是 `u32 elapsed_counter` 单调累加，多模块同时调用 `RTI_SlotElapsed(5)` 不会冲突（每个模块独立追踪自己的 slot），但**共享同一 period 的 slot 数量受限（典型 64 个）**。详见 `docs/RTI_GUIDE.md`。

---

## 4. 延时执行（RTI_Defer API）

需要"过 N ms 后再做某事"的场景用 `RTI_Defer`：

```c
#include "drv_api/rti_defer/rti_defer.h"

static void my_on_event(void)
{
    LOG_I("event happened, defer 50ms");
    RTI_Defer(50u, my_deferred_cb, NULL);   /* 50ms 后调 my_deferred_cb(ctx) */
}

static void my_deferred_cb(void *ctx)
{
    LOG_I("deferred callback ran");
}
```

**限制**：
- 同一时间点只能排 1 个 defer（单实例）；多次注册后入，后者覆盖前者
- defer 回调在 `Scheduler_Run()` 入口、`RTI_DeferTick()` 阶段执行
- defer 回调中再 `RTI_Defer()` 是允许的（覆盖旧值）

**替代方案**：用 RTI slot 周期任务代替 defer，更可预测。详见 `docs/RTI_DEFER_GUIDE.md`。

---

## 5. 调度器使用规约

### 5.1 tick() 必须保证

- **无阻塞**：不调 `OSIF_TimeDelay()`、`YTM_DELAY_xms()` 等任何 busy wait
- **无长路径**：tick 内不要遍历 > 64 元素数组、不要做浮点 log
- **可重入友好**：调用的子模块（如 RTI_Defer）必须不回调进 `Scheduler_Run`

### 5.2 模块共享数据

- **禁止**用全局可变量在不同模块 tick 间通信
- 用 `Signal_Get` / `Signal_Set`（app/signal/signal.h）
- 用 RTI slot 协调触发时机

### 5.3 IGN OFF / Standby 路径

KL15 掉电后调度器会：
1. 各模块 `standby()` 清理
2. `__WFI()` 等待 CAN RX / IGN 唤醒
3. 中断里复位 WDG、置 cold_boot=1 的 mcu_init 路径（如有）

详见 `docs/ARCHITECTURE.md` § 4。

---

## 6. 冷启动调用顺序（实际 main.c）

```
Reset
  |
  v
BSP_Init
  |
  v
UTILITY_PRINT_Init
  |
  v
RTI_Init                        <- SysTick 1ms tick
  |
  v
WDG_DRV_Init
  |
  v
Scheduler_Init                  <- mcu_init 所有模块
  |
  v
Scheduler_WakeupInit            <- wakeup_init 所有模块
  |
  v
Scheduler_OnIgnOn               <- on_ign_on 所有模块（仅 IGN=1）
  |
  v
for (;;) Scheduler_Run();       <- tick + RTI_DeferTick
```

---

## 7. 重入保护（Phase 1 / A6）

`Scheduler_Run()` 入口检查 `s_sched_depth`：
- 若非 0（已被另一调用占住），**死循环 → WDG 复位**
- 退出时回 0

```c
void Scheduler_Run(void) {
    if (s_sched_depth != 0u) {
        for (;;) { /* WDG reset */ }
    }
    s_sched_depth = 1u;
    /* ... 实际工作 ... */
    s_sched_depth = 0u;
}
```

**触发场景**：
- 模块 tick 内同步等待某事件，事件回调里又调 `Scheduler_Run()`（应改为 RTI_Defer）
- ISR 内代码意外调 `Scheduler_Run()`（应通过 Signal_Set 通知 tick 侧处理）

---

## 8. 已知问题（详见 SCHEDULER_ISSUES.md）

| ID | 摘要 | 状态 |
|---|---|---|
| - | tick 并发 slot 冲突（共享 period slot 池） | 已知，文档化 |
| - | tick 长度不可中断（无抢占） | 设计取舍，不改 |
| - | 模块数硬编码（k_module_count 编译期定） | 已知 |
| - | defer 单实例限制 | 已知，文档化 |
| - | SCHED_BUDGET 统计依赖 OSIF_GetMilliseconds 1ms 分辨率 | Cortex-M33 后续可加 DWT CYCCNT 提升精度 |

---

## 9. 验证清单（新增模块必跑）

- [ ] `mod_my.c` 内 5 个 hook 实现完整（或显式 NULL）
- [ ] `scheduler.c` 加 `extern const mod_desc_t mod_my;`
- [ ] `g_sched_modules[]` 末尾追加 `&mod_my`
- [ ] IAR V9.40.1 编译 0 错 0 警（确认 `__root` 符号被引用）
- [ ] 上电后 log 打印 `[XX] my` 模块名 + init OK
- [ ] `Scheduler_Run()` 不触发重入守卫

---

## 10. 相关 REVIEW 决议

详见 docs/REVIEW_NOTES.md：
- A6 Scheduler_Run 重入守卫
- A8 mod_desc_t hook 时序契约（初始化阶段同步安装，运行期不可变更）
- B2 prv_module_count 编译期常量
- C5 g_sched_modules[] + extern 双重源（决议保留，需改 .icf 才能消重）