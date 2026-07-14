# 信号总线使用指南

> Signal 总线**只**承担 CAN 相关数据
> 信号语义按三件套 (Signal_Get / Signal_GetStored / Signal_IsValid) 区分：
>   - 正常态 → Get 返回最新值、IsValid=true；
>   - 超时 / 未收 → Get 返回 0 (fallback)；GetStored 仍可读最近一次有效值（仪表降级显示）；
>   - 启动期 bitmap 全 1 = 全部超时，第一次成功 RX 后才清零。
> 任何业务数据（结构体 / 数组 / 标量）请直接使用模块自有或共享的 extern 全局变量；
> ARCHITECTURE **不**对"业务用 extern 全局"有任何 owner / 写者唯一性限制。
> 详见 `docs/ARCHITECTURE.md` §4。

## 0. 一句话规则

> **跨模块 CAN 报文值 → Signal；模块私有 / 业务运行时数据
> → 直接 extern 全局变量（无 owner 限制）；只读查找表 / 校准 →
> `static const`；持久化配置 → KV 存储。**

## 1. Signal 总线（仅 CAN）

- **承担内容**：
  - 207 个 `SIG_CAN_*`（DBC 自动生成的 CAN 信号）
  - 3 个 `SIG_CAN_RX_TIMEOUT_MAP_{LO,HI,HI2}`（接收超时 bitmap）
  - 4 个 `SIG_CAN_BUS_OFF/_COUNT/TX_ERR_CNT/RX_ERR_CNT`（总线健康）
- **API（六件套）**：
  - `Signal_Set(id, raw)` — 写入并置 `valid=true`、置 `ever_set=true`
  - `Signal_Get(id)` — **valid 时返回 raw**；超时 / 未收 / 越界 → 0 (fallback)
  - `Signal_GetStored(id)` — **强制**返回最近一次 `Signal_Set` 写入值（不看 valid）；
    用于仪表降级显示、超时前最后一帧、首屏兜底
  - `Signal_IsValid(id)` — `valid && ever_set` 才返回 true
  - `Signal_Invalidate(id)` / `Signal_InvalidateAll()` — 清 valid（保留 value + ever_set）
  - **`Signal_Reset(id)`**  — 彻底冷启动：清 value / valid / ever_set 三位。
    **仅** 给各模块 `prv_mcu_init` 使用，业务方不应主动调用。
- **写者唯一**：每个 `SIG_CAN_*` 由 mod_can_rx 在 RX 派发时写一次。
  消费方只 `Signal_Get` / `GetStored` / `IsValid`，无锁。

## 2. 启动期清零语义（当前约定）

| 模块 | mcu_init 清零 | 用法 |
|---|---|---|
| `mod_can_rx` | 3 × timeout map (`Signal_Set(0xFFFFFFFFu)`) — 全 1 = 启动期全部超时；207 × `Signal_Reset(SIG_CAN_*)` — 冷启动默认 | F1 + F3 |
| `mod_can_tx` | 4 × bus-health (`Signal_Reset`) — 启动期默认 | F2 |

**为什么 bitmap 是 0xFFFF 而不是 0？** 
冷启动 → 50 ms tick 第一次跑前，所有 bit-N 都没有"实测超时判决"，
默认全超时比"全未超时"更安全 — 业务方如果只看 bitmap 会得到一致的超时视图，
直到第一次 50 ms tick 给出实测值为止。同时 `Signal_Get(SIG_CAN_*)` 因为
`Signal_Reset` 已经处于 valid=false 状态，仍然返回 0 fallback — 两套机制一致。

## 3. 超时驱动：有效 / 无效标志切换

```
+----------------------------------------------------------------+
|        CAN RX 50ms tick (can_rx.c::prv_check_timeouts)          |
|                                                                 |
|  last_rx_tick_ms[bit-N]      now - last > tmo_ms[bit-N]?       |
|            │                                │                   |
|            │ no                             │ yes (rising edge) |
|            ▼                                ▼                   |
|     bit-N 留 0                       bit-N 置 1                |
|                                       CAN_Db_Invalidate        |
|                                        SignalsOnMsgTimeout(    |
|                                          ipk_idx )              |
|                                       ↓                          |
|                          该消息的所有 signal 槽位:              |
|                            valid = false (ever_set 保留)       |
|                            value 保留 (供 GetStored 读)        |
+----------------------------------------------------------------+
```

> **`GetStored`** 的语义：超时瞬间 value 不被清零，仪表可以继续显示
> "上一次有效帧"，直到下一次正常 RX 重新覆盖。

## 4. CAN 报文超时查询

`Signal_Get(SIG_CAN_RX_TIMEOUT_MAP_*)` 只能看整个 bitmap 的一位。
需要 per-id 查询时用 `app/can/can_rx.h` 提供的两个 API：

```c
#include "app/can/can_rx.h"

bool CanRx_IsMsgTimedOut(u32 can_id);                     // bool
c02b2_result_t CanRx_GetMsgFreshness(u32 can_id,         // enum
                                     can_rx_freshness_t *out);

typedef enum {
    CAN_RX_FRESH_NEVER     = 0,  // 启动后未收到过
    CAN_RX_FRESH_OK        = 1,  // 当前在 timeout 窗口内
    CAN_RX_FRESH_TIMED_OUT = 2,  // 一旦收到过，但当前已超时（值保留）
} can_rx_freshness_t;
```

**关键点**：`CAN_RX_FRESH_TIMED_OUT` 时 `Signal_Get` 返回的信号值
**仍是超时前的最后一帧**（保留未动，方便模块"降级显示上一次
正确值"）。`Signal_GetStored` 在任何超时状态下都返回该最后一次值。

## 5. 业务数据通道

任何模块私有 / 共享的数据（结构体、数组、标量、KV 备份过的配置）：

```c
/* 哪模块要写就哪模块写, 哪模块要读就 extern 拉过去 */
typedef struct { u32 kl30_mv; u8 mode; bool ign_on; } power_snap_t;
extern power_snap_t g_power_snap;
```

- 模块可在自己职责范围内读写自己维护的全局变量，不再在硬约束列表里。
- 多模块共享同一份数据时, 写入责任由该数据实际持有者决定。
- 全局变量初始值建议在负责模块的 `prv_mcu_init` 中显式赋一次（不依赖 BSS 清零）。

## 6. 其它通道（保持不变）

- **只读查找表 / 校准数据**：放在 `.h` 里以 `static const` 暴露（每个 .c 各自一份）
- **持久化配置**：`app/storage/kv.h::KV_Get/Set/Commit`
- **流式 / 帧数据**：ring buffer + Signal 通知
- **事件**：`Event_Post()` (后续批次)

## 7. 决定数据该走哪条通道

```
需要被另一个模块读取？
  ├─ 否 → 模块内部 static 变量
  └─ 是 → 数据来自 CAN 报文？
              ├─ 是 → Signal_Set/Get/GetStored/IsValid(SIG_CAN_*, ...)
              │     （超时用 CanRx_IsMsgTimedOut / GetMsgFreshness）
              └─ 否 → 直接 extern 全局变量（无限制）
                       或 模块 getter/setter（需要做参数校验或原子拷贝时）
```

## 8. 信号 API 速查

```c
/* 1. 写 */
(void)Signal_Set(SIG_CAN_EMS_EngineSpeedRPM, raw_value);

/* 2. 正常使用：fallback-0 */
u32 rpm = Signal_Get(SIG_CAN_EMS_EngineSpeedRPM);

/* 3. 仪表降级显示：保留最近一次 */
u32 rpm_last = Signal_GetStored(SIG_CAN_EMS_EngineSpeedRPM);

/* 4. 门控逻辑 */
if (Signal_IsValid(SIG_CAN_EMS_EngineSpeedRPM)) { ... }

/* 5. 主动失效（极少见，主要给 can_rx 50ms tick 用） */
Signal_Invalidate(SIG_CAN_EMS_EngineSpeedRPM);

/* 6. 模块初始化清零（F1/F2/F3） */
Signal_Reset(SIG_CAN_EMS_EngineSpeedRPM);   /* 仅 mcu_init 用 */
```

## 9. 反模式

| 反模式 | 正确做法 |
|---|---|
| 跨模块业务数据（非 CAN 信号）| 直接 extern 全局变量，或模块 getter/setter |
| 在 prv_mcu_init 之外调 Signal_Reset | 仅冷启动期推荐；业务方一般不需要 |
| 冷启动期用 `Signal_InvalidateAll()` 广播清零 | 各模块的 `prv_mcu_init` 改用 `Signal_Reset` 逐个清零自己拥有的 SIG_CAN_* |
| 把 `SIG_MAX` 当数组下标 | 用 `Signal_*` API，内部已经边界检查 |


## 10. 历史变更

见 `docs/CHANGELOG.md`。
