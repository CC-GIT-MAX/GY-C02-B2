# Scheduler_Run Known Issues

> 状态: **未处理 (Open)** — 等待真实业务模块接入前统一重构
> 创建: 2026-07-09
> 创建者: Codex (5ec3470..4442642 commit 阶段的代码审查)

`app/scheduler/scheduler.c` 的 `Scheduler_Run()` 是整个 C02-B2 固件的唯一调度入口。
当前架构在 CAN TX 验证通过(2026-07-08 23:17)后基本能跑,但**在多模块接入后会暴露出 6 个问题**。
本文档逐条记录,作为未来重构的待办清单。

---

## 背景

### 调用路径

```c
for (;;) {              // main loop
    Scheduler_Run();
    __DSB(); __WFI(); __ISB();
}

void Scheduler_Run(void) {
    RTI_DeferTick();    // 派发一次性延迟回调
    for (uint32_t i = 0; i < g_module_cnt; i++) {
        const mod_desc_t *m = g_modules[i];
        if (m->tick) m->tick();
    }
}
```

### 当前注册模块

| 序号 | 模块 | tick 内 RTI 用法 |
|---|---|---|
| 0 | `mod_template` | `RTI_IsElapsed(RTI_10MS)` + `RTI_IsElapsed(RTI_100MS)` |
| 1 | `mod_can_rx` | `RTI_IsElapsed(RTI_5MS)` + `RTI_IsElapsed(RTI_50MS)` |
| 2 | `mod_can_tx` | 自管 sweep_tick_ms(不依赖 RTI_IsElapsed) |

### 关键设计原则

scheduler.c 注释原话:

> `Order is significant for two things:`
>  `- Log readability (init order printed top-to-bottom)`
>  `- Tick data flow: can_rx runs BEFORE can_tx so the RX dispatcher publishes fresh signals that can_tx can read.`

但文件**末尾**又写:

> `Order is significant for log readability but not for correctness.`

这两条互相矛盾。**实际行为**:顺序对数据流正确性是关键的,见问题 3。

---

## 问题列表

| # | 严重 | 简述 | 状态 |
|---|---|---|---|
| 1 | 🔴 高 | RTI_IsElapsed slot 是 file-scope 单例,新模块会撞车 | Open |
| 2 | 🟡 中 | 注释自相矛盾(数据流 vs. correctness) | Open |
| 3 | 🟡 中 | 注册顺序是隐式契约,无工具帮助 | Open |
| 4 | 🟢 低 | RTI_DeferTick 无嵌套保护 | Open |
| 5 | 🟢 低 | 模块 tick 无 watchdog 预算 | Open |
| 6 | 🟢 低 | 注释里 sample-once 模式没有 helper 强制 | Open |

---

## 问题 1 — RTI_IsElapsed slot 共享 🔴

### 现象

`app/rti/rti.c` 的 `static uint32_t s_last[8]` 是 **TU 内单例**,所有模块共享同一组 slot。
任何两个模块调用同一个 period,后调用方拿到的是**已经被前调用方 stamp 过的 tick**。

### 触发场景

| 场景 | 现状 | 后果 |
|---|---|---|
| 当前注册 | can_rx 5ms/50ms / can_tx 10ms(自管)/ mod_template 10ms+100ms | 无直接冲突(can_tx 已自管) |
| 新模块要 100ms | 与 mod_template 抢 | mod_template 100ms task 死锁 |
| 新模块要 1s | 无冲突 | OK |
| 新模块要 5ms | 与 can_rx 抢 | 一个 drain 丢失 |
| 新模块要 10ms | 与 mod_template 抢 | mod_template 10ms task 死锁 |
| 2 个新模块要 5ms | 互相干扰 | 不确定 |

### 触发证据

- 旧版 `mod_can_demo` 同时占 `RTI_100MS` 和 `RTI_1000MS`,与 `mod_template` 100ms 冲突;
  `mod_can_demo` 删掉后问题暂时消失,但根因还在。
- 注释里也明说 "we MUST sample each period slot exactly once per tick",
  说明作者知道这个限制,但**没有 API 强制**。

### 修复方案

**方案 B(RTI_Defer 一统) — 推荐**

完全废弃 `RTI_IsElapsed` 周期 slot,统一用 `RTI_Defer` 的"deadline 列表"。

```c
/* 模块 mcu_init 里 */
(void)RTI_DeferRepeat(10u, prv_10ms_cb, NULL);  // 每 10 ms 调一次
(void)RTI_DeferRepeat(100u, prv_100ms_cb, NULL);
```

需要 `rti_defer.c` 加一个 `RTI_DeferRepeat(period_ms, cb, ctx)`,
callback 内自动 re-arm 自己。

**优点**:
- 复用现有 `rti_defer.c` 代码
- 天然 per-caller 隔离(每个 callback 一个 slot)
- API 一致(模块只需要一个 Defer API)

**缺点**:
- 8 slot 池可能不够,需要扩
- 每次 Defer 触发有 O(n) 扫描开销

**方案 A(slot-per-caller 索引)**

```c
typedef enum { RTI_SLOT_0, ..., RTI_SLOT_7 } rti_slot_t;
void RTI_BindPeriod(rti_slot_t slot, rti_period_t period);
bool RTI_IsElapsedSlot(rti_slot_t slot);  // 独立 slot
```

每个模块在 `mcu_init` 里绑定一个独占 slot,从 8 个静态 slot 池里取,模块间互不干扰。

**短期缓解(不重构)**

不动 `RTI_IsElapsed` 本身,在 `rti.h` 注释里加 WARNING,并提供
`RTI_BindSlot(slot_id, period_ms)` 强制每个模块显式占位。

### 影响面

- `app/can/can_rx.c`(用 5ms/50ms)
- `app/can/can_tx.c`(当前自管,回归统一 API 需评估)
- `app/mod_template/mod_template.c`(用 10ms/100ms)
- 任何未来新模块

---

## 问题 2 — 注释自相矛盾 🟡

### 现象

`app/scheduler/scheduler.c` 内对注册顺序的描述前后矛盾:

```c
/** ... Order is significant for two things:
 *   - Log readability (init order printed top-to-bottom)
 *   - Tick data flow: can_rx runs BEFORE can_tx so the RX
 *     dispatcher publishes fresh signals that can_tx can read.
 */
const mod_desc_t * const g_modules[] = { ... };
```

文件末尾又写:

```c
/* Order is significant for log readability but not for correctness. */
```

实际行为:顺序**对数据流正确性是关键**(见问题 3)。

### 修复

最低成本:删掉末尾那行错注释,统一口径。
最好:加 `static_assert` 在 `Scheduler_Run` 里,逐项检查
`g_modules[]` 中 `&mod_can_rx` 的 index < `&mod_can_tx` 的 index。

### 影响面

`app/scheduler/scheduler.c`(只 1 行注释,或 +5 行 assert)

---

## 问题 3 — 注册顺序是隐式契约,无工具帮助 🟡

### 现象

当前 `g_modules[]` 注册顺序就是数据流方向。但:

1. **新模块作者必须读所有现有模块的 tick 实现才能知道插哪里**;
2. **没有"前置依赖"声明机制**;
3. 注释里虽然提示了"can_rx before can_tx",但对其他未来模块(读 can_rx 后的 signal? 读 can_tx 后的 signal?)无系统说明。

### 逆行风险

```c
/* 危险: mod_diag 假设它要读 can_rx 解析后的 signal */
g_modules[] = {
    &mod_diag,     // 0  ← 错:此时 can_rx 还没跑
    &mod_template,
    &mod_can_rx,
    &mod_can_tx,
};
```

→ mod_diag tick 触发时,can_rx 还没跑,signal 未更新 → 读到旧值。

### 修复方案

**方案 D(显式声明依赖)— 长期**

```c
typedef struct mod_desc_s {
    const char *name;
    void (*mcu_init)(uint8_t);
    void (*wakeup_init)(void);
    void (*on_ign_on)(void);
    void (*tick)(void);
    void (*standby)(void);
    const char * const *depends;  /**< NULL-terminated list of module
                                       names whose tick must run first.
                                       Scheduler does topo-sort at boot. */
    uint8_t priority;             /**< tiebreaker; lower = earlier. */
} mod_desc_t;
```

`Scheduler_Init` 阶段做 topological sort,运行时按依赖序遍历。
检测到循环依赖时编译期 `static_assert` 报错。

**方案 F(文档化 + 注释)— 短期**

- 在 `scheduler.c` 头部用文字明确"上游→下游"约定
- 加 `static_assert(g_module_can_rx_idx < g_module_can_tx_idx)`
- 加 README 段落解释如何选位置

### 影响面

`app/scheduler/scheduler.c` + `app/scheduler/scheduler.h`

---

## 问题 4 — RTI_DeferTick 无嵌套保护 🟢

### 现象

`Scheduler_Run` 第一步 `RTI_DeferTick()` 派发所有到期的延迟回调。

**风险**:
- 某个 defer callback 在内部调用了 `Scheduler_Run()`(虽不应该,但 RTI_Defer 没禁止)
  → 嵌套调度,栈溢出
- callback 内修改了 `m->tick()` 用的 signal → 当前 tick 看到新数据(上次的 callback 刚改),
  违反因果

### 修复

```c
static uint8_t s_in_tick = 0u;

void RTI_DeferTick(void) {
    if (s_in_tick) return;  // nested: skip
    s_in_tick = 1u;
    /* ... existing scan + dispatch ... */
    s_in_tick = 0u;
}
```

### 影响面

`app/rti/rti_defer.c`(3 行改动)

---

## 问题 5 — 模块 tick 无 watchdog 预算 🟢

### 现象

`Scheduler_Run` 一口气跑完所有模块 tick。如果其中一个模块 tick 卡住
(如 `prv_drain` 死循环),整个 super-loop 死掉,只能靠硬件 WDG reset。

### 现状

- SysTick_Handler 喂狗(每次 1ms),基础保护有
- can_rx 的 `prv_drain` 有 `drained >= DEMO_RX_DRAIN_MAX_FRAMES` 上限
- can_tx 的 10ms sweep 最多 9 条 TX(隐含上限)
- mod_template 无保护

### 修复

- 加可选 `tick_watchdog` 字段:`uint16_t tick_max_us;`
  - 模块声明"我的 tick 不应超过 N us",超出则 `LOG_W` 报警
- 短期:每个模块 tick 内自带超时保护

### 影响面

`app/scheduler/scheduler.h`(加 1 字段) + 各模块

---

## 问题 6 — sample-once 模式没有 helper 强制 🟢

### 现象

模块 tick 内多次调用同一 `RTI_IsElapsed(period)` 时,**只有第一次返回正确值**,
之后全是 false(slot 已被前一次 stamp)。

注释里反复提示(见 `mod_can_demo.c` 删掉前的 100ms/1000ms 调用),但**没有 helper
强制**。

### 修复

加一个 `RTI_SampleOnce(rti_period_t, bool *sampled)` helper:

```c
/**
 * @brief  Sample a period slot exactly once per super-loop iteration.
 *         Returns the slot state at the time of the first call; subsequent
 *         calls with the same period return the cached value.
 */
bool RTI_SampleOnce(rti_period_t period, bool *sampled_first);
```

或更简单:在 `Scheduler_Run` 入口处,自动重置所有 RTI slot。

### 影响面

`app/rti/rti.c` + `app/rti/rti.h`

---

## 推荐处理顺序

1. **问题 2(改注释 + static_assert)** — 5 分钟,最低成本,先做
2. **问题 4(RTI_DeferTick 嵌套保护)** — 10 分钟,3 行代码
3. **问题 1(短期缓解:`RTI_BindSlot` 警告)** — 30 分钟,不影响现有调用
4. **问题 1(根治:RTI_Defer 一统)** — 半天,要重写 3 个模块的 tick
5. **问题 3(显式依赖)** — 1 天,涉及 scheduler.h 接口扩展
6. **问题 5/6** — 与问题 1 一起做,不单独提

## 关联 commit

- 5ec3470 `fix(can): self-paced 10ms sweep gate for can_tx cyclic task` — 临时绕开问题 1
- 7ecb84f `fix(can): zero-init flexcan_data_info_t + wait MCR.NOTRDY after Init` — NOTRDY 部分已在 62f92af revert
- 4442642 `docs(can): polish CanIf_RxPending() doxygen`