# Scheduler + RTI 并发修复设计 Spec

> 目标:在不破坏现有架构约束的前提下,解决多模块共用同一 period 时
> `RTI_IsElapsed()` slot 互相覆盖导致后注册者永远拿不到 true 的问题;
> 同时让"新增模块"不再需要手改 `app/scheduler/scheduler.c`。

> 适用版本:`codex/followup-issues` (HEAD = 0723e3a)
> 日期:2026-07-09

## 1. 背景与现状(摘要)

### 1.1 关键事实

- **scheduler**:`Scheduler_Run()` 每 super-loop tick 顺序调用每个模块的 `tick()`。模块表 `g_modules[]` 手写在 `scheduler.c` 里,新增模块要改 `scheduler.c`。
- **RTI_IsElapsed**:`app/rti/rti.c` 维护 8 个 static slot,每个 `rti_period_t` 对应唯一一个 slot。**多模块共用同一 period 时,后调用者覆盖前调用者的 last tick**,前一个模块的 period tick 永远拿不到 true。
- **模块怎么注册时间任务**(现状):
  - `mod_can_rx` 用 `RTI_5MS` + `RTI_50MS`(独占,暂无冲突)
  - `mod_can_tx` 自维护 `sweep_tick_ms`,主动避开 `RTI_IsElapsed`(注释明确说明原因)
  - `mod_template` 用 `RTI_10MS` + `RTI_100MS`(demo 独占)
  - `mod_can_demo` 自维护 `last_tick_ms`(1 s tick)
- **为什么没人踩雷**:模块少 + 大家自觉避开共享 slot。一旦业务模块真正多起来,`RTI_100MS` 这种常用 period 必然撞车。

### 1.2 架构约束(不动)

- 业务代码不引入动态分配(裸机,M33 @ 120 MHz)
- 业务代码不引入 RTOS
- 不修改 `board/`、`CMSIS/`、`middleware/`、`platform/`、`rtos/`
- `tick()` 内禁止阻塞延时(架构 §10)
- IAR V9.40.1 必须 0 错 0 警
- 每 commit 一个根因主题,中文双语

## 2. 目标

### 2.1 必须达成

1. **并发 period 不冲突**:`mod_a` 和 `mod_b` 各自调 `RTI_IsElapsed(RTI_100MS)` 互不干扰,各自独立计时。
2. **新增模块零 touch `scheduler.c`**:模块开发者只需在自己的 `.c` 加一行注册宏,`g_modules[]` 自动填好。
3. **优先级 / 时间预算默认关**:由 `SCHED_BUDGET_EN` 宏开启,关闭时零额外开销(分支可被编译器消除)。
4. **不破坏 RTI_Defer 现有语义**:8-slot 池,一次性延迟,不变。

### 2.2 不做

- 不做完整优先级调度(模块声明 `priority` 字段、按 priority 排 tick)。`SCHED_BUDGET_EN` 开启时只做"超预算告警 + 累加计数"。
- 不把 RTI_Defer 改成可周期。
- 不做模块动态注销。

## 3. 设计方案

### 3.1 RTI_IsElapsed caller-private slot

**核心改动**:把 `RTI_IsElapsed(period)` 的状态从"全局共享 slot"改为"调用者私有 slot"。

实现方案:**slot 状态由调用方持有**(通过 slot 句柄)。RTI 提供两个 API:

```c
/* 新 API(本次唯一 API,旧 RTI_IsElapsed 已删除) */
typedef struct { void *_priv; } rti_slot_t;
rti_slot_t RTI_OpenSlot(rti_period_t period);
bool       RTI_SlotElapsed(rti_slot_t *slot);
```

#### 3.1.1 内存模型与 period 占用规则

- RTI 内部维护 **固定池**:`s_slots[N]`(默认 `N = 64`),每个 slot 是 `{ uint32_t last; bool inited; rti_period_t period; }`。
- `RTI_OpenSlot(p)` 在池中找第一个未占用的 slot,绑定 period,返回句柄。占用后不可被其他 caller 抢占,直到模块卸载(本工程不卸载)。
- 池满返 `{ NULL }`(无效句柄),`RTI_SlotElapsed` 对无效句柄返 false 并 `LOG_W`。

- **同一 period 占不同 slot**:`RTI_OpenSlot(RTI_100MS)` 调用 N 次 → 占 N 个 slot,各自独立计时。模块 A 和模块 B 都想要"每 100ms 跑一次" → 各调一次 `OpenSlot`,互不干扰。这是新 API 的核心承诺。
- **同一个 slot 只能绑一个 period**:同一句柄无法切换 period;要换 period 必须先 `CloseSlot`(本工程暂不实现,因为模块不卸载)。
- **池满行为**:64 个 slot 都被占用后再调 `OpenSlot` → 返无效句柄,`RTI_SlotElapsed` 对无效句柄返 false 并 LOG_W,不影响其他模块。
- **触发时刻抖动**:同一 period 的多个 slot 实际触发点会被 `Scheduler_Run` 的顺序遍历拉开 1-2ms。如果业务硬要求"对齐触发",需另起时间戳同步机制,不在本 spec 范围。

#### 3.1.2 旧 API 已删除

RTI_IsElapsed 在本次提交中**完全删除**。can_rx.c 的 RTI_5MS/RTI_50MS、mod_template.c 的 RTI_10MS/RTI_100MS 全部切到 RTI_OpenSlot + RTI_SlotElapsed(Step 1 一并提交)。后续业务模块开发者**只能使用新 API**。
- 句柄 `{ void *_priv }` 只 4/8 字节,模块持有一个指针即可
- 不需要 free(没有注销路径)

### 3.2 模块懒注册(SCHED_REGISTER 宏)

#### 3.2.1 思路

**每个模块的 `.c` 文件加一行**:

```c
SCHED_REGISTER(mod_xxx);
```

宏在文件作用域展开为:

```c
#pragma location=".sched_modules"
static const mod_desc_t * const _sched_ref_mod_xxx = &mod_xxx;
```

#### 3.2.2 IAR section 配置

IAR ILINK 链接器需要把 `.sched_modules` 段聚合到一块连续内存。在 `.icf` 里加一段:

```
place in ROM_region { readonly section .sched_modules };
define exported symbol __sched_modules_start = addr of .sched_modules start;
define exported symbol __sched_modules_end   = end of .sched_modules;
```

`.icf` 改 1 段即可,不破坏现有内存布局。

#### 3.2.3 scheduler.c 改动

```c
/* 不再手写 g_modules[] = { &mod_a, &mod_b, ... }; */
/* 改为: */

extern const mod_desc_t * const __sched_modules_start[];
extern const mod_desc_t * const __sched_modules_end[];

void Scheduler_Init(void) {
    const uint32_t n = (uint32_t)(__sched_modules_end - __sched_modules_start);
    LOG_I("init: %u modules", (unsigned)n);
    for (uint32_t i = 0; i < n; i++) {
        const mod_desc_t *m = __sched_modules_start[i];
        LOG_I("  [%02u] %s", (unsigned)i, m->name);
        if (m->mcu_init) m->mcu_init(1u);
    }
}
```

`Scheduler_WakeupInit / OnIgnOn / Run / Standby` 同样改成遍历 `__sched_modules_start/end`。

#### 3.2.4 注册顺序

IAR section 内 symbol 顺序由链接顺序决定(`#pragma location` 单独放置时,顺序按文件在链接命令行的顺序;`.c` 文件中多个 `SCHED_REGISTER` 按文件内出现顺序)。

**契约**:模块开发者想控制顺序,在 `.c` 顶部注释里写明依赖关系(如"必须在 mod_uart 之后");若不写,顺序由链接顺序决定。

#### 3.2.5 SCHED_REGISTER 宏定义

放在 `app/scheduler/scheduler.h`:

```c
#if defined(__IAR_SYSTEMS_ICC__)
  #define SCHED_REGISTER(_mod)                                            \
      _Pragma("location=\".sched_modules\"")                               \
      static const mod_desc_t * const                                     \
      _sched_ref_##_mod = &(_mod)
#else
  #define SCHED_REGISTER(_mod)                                            \
      __attribute__((used, section(".sched_modules")))                    \
      static const mod_desc_t * const                                     \
      _sched_ref_##_mod = &(_mod)
#endif
```

#### 3.2.6 每个模块的迁移

4 个现有模块每个 `.c` 加一行:

```c
SCHED_REGISTER(mod_template);   /* mod_template.c 末尾 */
SCHED_REGISTER(mod_can_rx);     /* can_rx.c 末尾 */
SCHED_REGISTER(mod_can_tx);     /* can_tx.c 末尾 */
SCHED_REGISTER(mod_can_demo);   /* mod_can_demo.c 末尾 */
```

`scheduler.c` 删除手写 `g_modules[]`、删除 `extern const mod_desc_t mod_xxx;` 4 个 forward declaration,改为引用 `__sched_modules_start/end`。

### 3.3 时间预算(SCHED_BUDGET_EN 宏)

#### 3.3.1 行为

`Scheduler_Run()` 每个 super-loop tick:

1. 遍历 `__sched_modules_start/end`
2. 对每个模块:
   - 记 `tick_start_us`(读 DWT->CYCCNT,M33 有 DWT,120 MHz)
   - 调 `m->tick()`
   - 算 `tick_dur_us = tick_start_us - now`(读 DWT->CYCCNT 转 us)
   - 若 `tick_dur_us > SCHED_BUDGET_US`(默认 1000 us):
     - `LOG_W("[SCH] %s tick overrun: %u us", m->name, tick_dur_us)`
     - 累加 `m->_budget_overrun_cnt`(静态字段)
3. 若 `SCHED_BUDGET_EN=0`,整个 if 分支不编译。

#### 3.3.2 配置文件

`scheduler.c` 顶部:

```c
#ifndef SCHED_BUDGET_EN
  #define SCHED_BUDGET_EN  0   /* 默认关,打开需要显式 define */
#endif
#ifndef SCHED_BUDGET_US
  #define SCHED_BUDGET_US  1000u
#endif
```

build 选项里加 `-DSCHED_BUDGET_EN=1` 即可打开。

#### 3.3.3 mod_desc_t 改动

```c
typedef struct mod_desc_s {
    const char *name;
    void (*mcu_init)(uint8_t cold_boot);
    void (*wakeup_init)(void);
    void (*on_ign_on)(void);
    void (*tick)(void);
    void (*standby)(void);
#if SCHED_BUDGET_EN
    uint32_t _budget_overrun_cnt;   /**< cumulative overrun count */
    uint32_t _budget_last_dur_us;   /**< last tick() duration in us */
#endif
} mod_desc_t;
```

#### 3.3.4 超预算行为

**决策**:仅告警 + 累加计数器,不主动跳过下一个模块(避免级联效应)。用户日志里能看到是哪个模块超了。

## 4. 影响面

### 4.1 改的文件

| 文件 | 改动 |
|---|---|
| `app/rti/rti.h` | 删除 `RTI_IsElapsed`,新增 `RTI_OpenSlot / RTI_SlotElapsed / rti_slot_t` |
| `app/rti/rti.c` | 加 `s_slots[RTI_SLOT_POOL_SIZE]` 池;删除旧 `RTI_IsElapsed` 实现 |
| `app/scheduler/scheduler.h` | 加 `SCHED_REGISTER` 宏;`mod_desc_t` 加 `#if SCHED_BUDGET_EN` 字段;`SCHED_BUDGET_EN/US` 宏默认值 |
| `app/scheduler/scheduler.c` | 删手写 `g_modules[]`;改用 `__sched_modules_start/end` 遍历;`Scheduler_Run` 加预算统计(可选编译) |
| `app/mod_template/mod_template.c` | 末尾加 `SCHED_REGISTER(mod_template);` |
| `app/can/can_rx.c` | 末尾加 `SCHED_REGISTER(mod_can_rx);` |
| `app/can/can_tx.c` | 末尾加 `SCHED_REGISTER(mod_can_tx);` |
| `app/mod_can_demo/mod_can_demo.c` | 末尾加 `SCHED_REGISTER(mod_can_demo);` |
| `EWARM/C02_B2.icf`(或等价文件) | 加 `.sched_modules` section 聚合 + `__sched_modules_start/end` 导出 |

### 4.2 不改的文件

- `board/` `CMSIS/` `middleware/` `platform/` `rtos/`
- `app/main.c`(业务模块通过宏自动注册,main 不感知)
- `app/drv_api/`(驱动接口不动)
- `app/can/` 业务逻辑(只加宏)
- `app/signal/` `app/storage/` `app/log/`

### 4.3 测试覆盖

- `tests/test_rti_slot.py`(新增):Python 模拟 RTI 池行为,验证 caller-private slot 互不干扰
- IAR 编译 0 错 0 警(每次 commit 前必须)
- 烧录 + CANalyzer 联调,跑现有 `mod_can_demo`,确认 sweep 输出未变
- 加 1 个 demo 模块 `mod_rti_demo`,验证两个模块同 `RTI_100MS` 各自独立计时

## 5. 风险与缓解

| 风险 | 缓解 |
|---|---|
| IAR `.sched_modules` 段聚合顺序不确定 | 文档约定"模块顺序无关紧要,除非注释显式声明依赖" |
| `.icf` 段定义遗漏 → 链接报 undefined | build 0 错 0 警是底线;`.icf` 改完必须 IAR 全量 build |
| `RTI_IsElapsed` 旧 API 仍被混用导致 slot 共享冲突 | 文档明确"新业务模块必须用 `RTI_OpenSlot`";旧调用方保持独占即可 |
| `SCHED_BUDGET_EN=1` 时读 SysTick 不够精度 | 用 `DWT->CYCCNT`(M33 有 DWT,120 MHz,32-bit),读到 us 级 |
| 模块开发者忘记加 `SCHED_REGISTER` | 启动日志 `init: 0 modules` 暴露;后续可加 link-time 检查 |

## 6. 实施步骤

1. **Step 1**:`rti.h/c` 加 caller-private slot,**删除旧 `RTI_IsElapsed`**;同步迁移 `can_rx.c`(`RTI_5MS`/`RTI_50MS`) + `mod_template.c`(`RTI_10MS`/`RTI_100MS`) 到 `RTI_OpenSlot/SlotElapsed`。commit。
2. **Step 2**:`scheduler.h/c` 加 `SCHED_REGISTER` 宏 + icf section 聚合;迁移 4 个现有模块。commit。
3. **Step 3**:`scheduler.h/c` 加 `SCHED_BUDGET_EN` 时间预算(默认关)。commit。
4. **Step 4**:加 `mod_rti_demo` 模块 + `tests/test_rti_slot.py`,验证并发 period slot 互不干扰。commit。
5. **Step 5**:文档更新:`docs/ARCHITECTURE.md`、`docs/RTI_DEFER_GUIDE.md`(扩写为 RTI_GUIDE,含 slot 新 API)。commit。

每个 step 一个 commit,中文双语标题。

## 7. 验收标准

- IAR V9.40.1 编译 0 错 0 警(开 / 关 `SCHED_BUDGET_EN` 都要测)
- 现有 `mod_can_demo` sweep 输出未变(回归测试)
- `mod_rti_demo` 两个 100ms 子任务各自独立计时,日志可验证
- `SCHED_REGISTER` 不在 `scheduler.c` 留任何手写 `&mod_xxx`
- 烧录跑通 CANalyzer demo(回归)

## 8. 待用户确认的问题

### Q1:`RTI_IsElapsed` 旧 API 是否在本次改动里直接删除?

- **A(已选定)**:删除,所有调用方改用 `RTI_OpenSlot / RTI_SlotElapsed`(本次 4 个模块都要改:`can_rx.c` 的 `RTI_5MS`/`RTI_50MS`、`mod_template.c` 的 `RTI_10MS`/`RTI_100MS`,Step 1 一并提交)。

### Q2:`mod_rti_demo` 是否要在本次提交?

- **A(已选定)**:加。完整演示并发 slot 互不干扰,作为后续模块开发者参考模板。

## 9. 参见

- `app/scheduler/scheduler.c` - 当前模块注册表
- `app/rti/rti.c` - RTI slot 实现
- `app/can/can_tx.c` - tick 内自维护 last tick 的现存做法
- `app/mod_template/mod_template.c` - tick 内 RTI_IsElapsed 用法范例
- `docs/ARCHITECTURE.md` - 架构约束
- `docs/RTI_DEFER_GUIDE.md` - RTI_Defer 使用指南
- `docs/COMMIT_CONVENTION.md` - commit 规范



