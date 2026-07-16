# 信号总线使用指南

> Signal 总线**只**承担 CAN 相关数据
> 信号语义按 v0.5 模型区分 (validity 走 timeout bitmap,不再保留 per-slot valid):
>   - 正常态 → Get 返回最新值、IsValid=true；
>   - 超时 / 未收 → Get 返回 init_value (INIT_DBC 默认) 或保留"超时前最后一帧"
>     (KEEP_LAST 可选); IsValid=false; HasEverReceived 独立查 ever_received map;
>   - 启动期 bitmap 全 1 = 全部超时; boot_done=0 (prv_check_timeouts 首次 tick 置 1)。
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
- **API（v0.5 五件套 + 1 cold-start）**：
  - `Signal_Set(id, raw)` — 写 RAW u32 进 slot；无 valid 守卫
  - `Signal_Get(id)` — 直返最近一次 Set 写入值；BSS 默认 0；
    超时后值由 policy 决定 (INIT_DBC -> init_value, KEEP_LAST -> 保留原值)
  - `Signal_IsValid(id)` — `boot_done && !timeout_bit`；无 timeout map 的 id 走
    `value != 0` 兜底 (BUS health)
  - `Signal_HasEverReceived(id)` — 查 SIG_CAN_RX_EVER_RECEIVED_{LO,HI,HI2};
    该 MSG 是否曾收到过有效帧 (KL15 off 前不重置)
  - `Signal_SetBootDone()` / `Signal_ResetBootDone()` / `Signal_IsBootDone()`
    — bootstrap 窗口 flag, 由 can_rx prv_check_timeouts / prv_standby 控制
  - **`Signal_Reset(id)`** — 写 `can_sig_descs_ipk[id-1].init_value` 进 slot。
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

## 3. 超时驱动 (v0.5: timeout bitmap 是 SoT)

```
+----------------------------------------------------------------+
|        CAN RX 50ms tick (can_rx.c::prv_check_timeouts)          |
|                                                                 |
|  last_rx_tick_ms[bit-N]      now - last > tmo_ms[bit-N]?       |
|            │                                │                   |
|            │ no                             │ yes (rising edge) |
|            ▼                                ▼                   |
|     bit-N 留 0                       bit-N 置 1                |
|                                       CanDb_InvalidateSignalsOn|
|                                        MsgTimeout(ipk_idx)    |
|                                       ↓                          |
|                              按 signal 级别 policy 决议:        |
|                              - INIT_DBC (默认) -> 写 init_value  |
|                              - KEEP_LAST (可选) -> 保留原值     |
|                                                                 |
|  prv_check_timeouts 首次进入 → Signal_SetBootDone() 标记        |
|  bootstrap 窗口结束,IsValid() 进入基于 bitmap 的判定。          |
+----------------------------------------------------------------+
```

> INIT_DBC 默认让上层 `Signal_Get()` 在超时后直接读到 DBC 默认值
> (例如 TPMS 0xFF = Invalid); KEEP_LAST 的 signal 通过 `CanDb_SetSignalTimeoutPolicy()`
> 在 mcu_init 之后切换,运行时业务方拿到的是"timeout 前最后一帧"。

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

**关键点**：`CAN_RX_FRESH_TIMED_OUT` 时 `Signal_Get` 返回的信号值：
- INIT_DBC signal → DBC init_value (e.g. TPMS_FLTyrePr = 0xFF);
- KEEP_LAST signal → "timeout 前最后一帧" raw (供"降级显示上一次正确值"用).
切换 KEEP_LAST 见 `CanDb_SetSignalTimeoutPolicy()`。

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
              ├─ 是 → Signal_Set/Get/IsValid/HasEverReceived(SIG_CAN_*, ...)
              │     （超时用 CanRx_IsMsgTimedOut / GetMsgFreshness;
              │      INIT_DBC vs KEEP_LAST 走 CanDb_SetSignalTimeoutPolicy）
              └─ 否 → 直接 extern 全局变量（无限制）
                       或 模块 getter/setter（需要做参数校验或原子拷贝时）
```

## 8. 信号 API 速查 (v0.5)

```c
/* 1. 写 */
(void)Signal_Set(SIG_CAN_EMS_EngineSpeedRPM, raw_value);

/* 2. 读取 (TX loopback 也用这个) */
u32 rpm = Signal_Get(SIG_CAN_EMS_EngineSpeedRPM);

/* 3. 门控 (timeout bitmap 推导) */
if (Signal_IsValid(SIG_CAN_EMS_EngineSpeedRPM)) { ... }
if (Signal_HasEverReceived(SIG_CAN_EMS_EngineSpeedRPM)) { ... } /* 曾收到过 */

/* 4. 选择超时策略 (mcu_init 之后, KEEP_LAST = 保留原值) */
CanDb_SetSignalTimeoutPolicy(SIG_CAN_EMS_EngineSpeedRPM, SIG_TIMEOUT_KEEP_LAST);

/* 5. 模块初始化冷启动默认值 (仅 prv_mcu_init 用) */
Signal_Reset(SIG_CAN_EMS_EngineSpeedRPM);   /* 写 DBC init_value */

/* 6. bootstrap 标志 (can_rx 内部使用) */
Signal_IsBootDone();   /* 0 = 启动期窗口, IsValid 一律 false */
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
