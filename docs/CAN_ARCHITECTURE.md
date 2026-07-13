# CAN 模块架构

> 状态：**权威**（所有 CAN 相关修改必须符合此文档）
> 创建：2026-07-13（Phase 5）
> 配合阅读：docs/ARCHITECTURE.md、docs/REVIEW_NOTES.md、app/drv_api/can/can_if.c

## 0. 设计目标

1. **业务层 (app/can/) 完全不接触 vendor SDK** —— 只见 can_if.h + can_db*.h + signal.h
2. **唯一允许 include flexcan_driver.h 的文件**：app/drv_api/can/can_if.c
3. **DBC 是 SoT** —— 派生表 (can_db_ipk_gen.{c,h}、signal.h CAN 段) 全由 tools/regen_can_artifacts.py 生成，禁止手改
4. **可重入 CanIf_Init()** —— 多次调用仅重置 RX ring，便于 boot 期间任意阶段调用
5. **冷启动 0 配置漂移** —— RX-FIFO filter 表、TX MB 分配、超时表均编译期常量 + #error 锁不变量

---

## 1. 分层与文件边界

```
+----------------------------------------------------------+
| app/can/can_rx.c   app/can/can_tx.c   app/mod_can_demo.c | 业务层
|     包含                                                  |
| app/drv_api/can/can_if.h  can_db.h  can_db_ipk_gen.h     |
|     包含                                                  |
| app/drv_api/can/can_if.c  can_db.c  can_db_codec.c       | 驱动 facade
|     包含                                                  |
| flexcan_driver.h  pins_driver.h  (vendor SDK)            |
+----------------------------------------------------------+
```

| 层 | 文件 | 职责 |
|---|---|---|
| 业务 | app/can/can_rx.c | 5ms drain + DBC 分发 + 50ms 超时监控 |
| 业务 | app/can/can_tx.c | 发送周期触发 + payload 重建 + 排队 |
| 业务 | app/mod_can_demo.c | 自检用例（CAN_DEBUG_LOG 宏控） |
| facade | app/drv_api/can/can_if.c | SPSC ring + FlexCAN 硬件初始化 + error cb |
| facade | app/drv_api/can/can_db.c | can_msg_descs_ipk[] SoT + DBC dispatch |
| facade | app/drv_api/can/can_db_codec.c | BitExtract / BitEncode / DecodeSignal / EncodeSignal |
| 生成 | app/drv_api/can/can_db_ipk_gen.{c,h} | AUTOGEN：从 DBC 生成 |
| 生成 | app/signal/signal.h CAN 段 | AUTOGEN：SIG_CAN_* + legacy 标注 |

**反向依赖禁令**：
- app/drv_api/can/*.c 不得 include app/can/can_*.h（实测通过；仅 linker 符号解析引用业务层）
- app/drv_api/can/*.c 不得 include app/signal/signal.c

---

## 2. 数据流

### 2.1 RX 路径

```
FlexCAN 硬件 RX-FIFO
    |  (ISR: prv_flexcan_rx_fifo_cb)
    v
prv_ring_push() <-- __DMB() release barrier
    |
    v  (tick: Scheduler_Run -> mod_can_rx.tick -> CanRx_Drain)
prv_ring_pop()  -> __DMB() acquire barrier -> can_msg_t
    |
    v
CanDb_DispatchByDb(msg_id, payload)  按 can_msg_descs_ipk[id].cb 分发
    |
    +-> registered cb (mod_xxx handler)  写入 SIG_CAN_*
    +-> Signal_Set(signal_id, raw_value)
```

### 2.2 TX 路径

```
Scheduler_Run -> mod_can_tx.tick -> CanTx_Tick
    |
    v  (按 g_can_tx_cycle_table[] 周期匹配)
CanTx_RebuildFromSignals(msg_id)
    |  Signal_Get(signal_id) 取 raw 值
    |  CanDb_EncodeSignal() 写 payload
    v
CanIf_Send(can_id, mb_idx, payload) <-- log 去重 + 冷却窗口
    |
    v  (6 个 TX MB 轮询: MB 26..31)
FLEXCAN_DRV_Send
    |
    v
FlexCAN 硬件 TX MB -> 总线
```

### 2.3 Error 路径

```
FlexCAN 硬件 -> FLEXCAN_DRV_InstallEventCallback -> prv_flexcan_err_cb
    |
    +- BUS_OFF      -> s_recovery_pending[ch] = true  排入恢复队列
    +- TX/RX WARN   -> log（无自动恢复动作）
    +- BIT_ERR      -> log（瞬时错误）
    +- RX_OVR       -> log + 严重告警
    +- ECC_ERR      -> log + WDG 立即重启

CanIf_RecoverPump（由 mod_can_tx.tick 调用）
    |
    v  检测 s_recovery_pending[ch]
CanIf_RecoverChannel(ch)
    |  FLEXCAN_DRV_Deinit(ch)  <- 复位控制器
    |  FLEXCAN_DRV_Init(ch)
    |  FLEXCAN_DRV_ConfigRxFifo(ch, ...)
    |  FLEXCAN_DRV_RxFifo(ch)  <- 重启 RX-FIFO（关键！）
    v
s_recovery_pending[ch] = false
```

---

## 3. 关键不变量（编译期锁）

| 常量 | 值 | 来源 | 锁不变量 |
|---|---|---|---|
| CAN_RX_RING_SIZE | 32 | 双倍 16 槽位 | 5ms tick × 64 ID 满载可吸收 |
| CAN_RX_FILTER_ELEMS_PUBLIC | 72 | RFFN=8 -> 8×9 | #error 锁 7+RFFN×2 不变量 |
| CAN_RX_FILTER_ELEMS_PRIVATE | 56 | RFFN=6 -> 8×7 | 同上 |
| TX MB 范围 | MB 26..31 | public/private 各 6 个 | prv_pick_tx_mb() 轮询 |
| RX-FIFO 起始 MB | MB 0 (public) / MB 0 (private) | RFFN 配置 | 硬件 spec |

**RFFN 公式**：filter table entries = 7 + RFFN×2，每表项最多 8 个 ID
- RFFN=8：72 表项，0x18..0x2F 共 24 个 ID 槽
- RFFN=6：56 表项，0x18..0x27 共 16 个 ID 槽

---

## 4. 时序契约

### 4.1 CanIf_Init() 启动时序

```
任意阶段可调用（BSP/DRV/RTI/Scheduler_Init 之前/之后/之间）：
  CanIf_Init() <- s_if_inited flag + 可重复调用
    |
    +- FLEXCAN_DRV_Init(ch, ...)
    +- FLEXCAN_DRV_ConfigRxFifo(ch, ...)
    +- FLEXCAN_DRV_InstallEventCallback(ch, ...)
    +- FLEXCAN_DRV_RxFifo(ch)  <- 必须！否则第一次收不到
```

**二次调用**仅重置 RX ring（head = tail = 0），不重复初始化硬件。

### 4.2 冷启动调用顺序（实际 main.c）

```
Reset
  |
  v
BSP_Init  (SystemInit, Flash wait states, 时钟树)
  |
  v
UTILITY_PRINT_Init  (UART 早期调试口)
  |
  v
RTI_Init  (SysTick 1ms tick + RTI slot 表)
  |
  v
WDG_DRV_Init  (喂狗窗口覆盖到 Scheduler_Run)
  |
  v
Scheduler_Init  (mcu_init 全部模块)
  |
  v
Scheduler_WakeupInit  (wakeup_init 全部模块)
  |
  v
Scheduler_OnIgnOn  (on_ign_on 全部模块 — IGN=1 时触发)
  |
  v
for (;;) Scheduler_Run()  -> __DSB(); __WFI(); __ISB();
```

**WDG 早启用是设计意图**（C10 决议保留）：早期 hang 立刻复位，WDG 喂狗窗口宽到覆盖到 RTI_Init 启动路径。

### 4.3 接收 ISR -> tick 衔接

- ISR 内 prv_ring_push() 仅写 ring，不分发
- tick 内 prv_ring_pop() 取出再分发到 DBC cb
- **ISR 与 tick 不共享状态**：避免 ISR 内长路径

---

## 5. 错误恢复机制

| 错误 | 检测 | 恢复动作 | 谁触发 |
|---|---|---|---|
| BUS_OFF | prv_flexcan_err_cb 看 ESR1.BOFF | s_recovery_pending[ch]=true | mod_can_tx.tick -> CanIf_RecoverPump() |
| TX/RX WARN | ESR1.TWRN/RWRN | 仅 log | ISR 内 |
| BIT_ERR | ESR1.BIT1_ERR | 仅 log | ISR 内 |
| RX_OVR | ESR1.RX_OVR | log + 告警 | ISR 内 |
| ECC_ERR | ESR1.RAMECC | log + WDG restart | ISR 内 |

**总线关闭恢复流程**（CanIf_RecoverChannel）：
1. FLEXCAN_DRV_Deinit(ch) — 关闭所有 FlexCAN 中断
2. FLEXCAN_DRV_Init(ch) — 复位控制器（清 BUS_OFF / 错误计数）
3. FLEXCAN_DRV_ConfigRxFifo(ch, ...) — 重建 filter
4. FLEXCAN_DRV_RxFifo(ch) — 启动 RX-FIFO

---

## 6. 接口清单

### 6.1 业务层 API（can_if.h）

```c
/* 初始化 — 可重复调用, 仅重置 ring */
c02b2_result_t CanIf_Init(void);

/* 配置 RX MB（单 MB 模式；本工程主用 FIFO，保留接口兼容） */
c02b2_result_t CanIf_ConfigRxMb(flexcan_instance_t ch, u8 mb_idx,
                                flexcan_id_table_t *filter);

/* 发送接口 */
c02b2_result_t CanIf_Send(flexcan_instance_t ch, u32 can_id,
                          const u8 *payload, u8 len);

/* 接收接口 */
bool CanIf_RxRingPop(can_msg_t *out);  // tick 侧调用

/* 错误恢复 */
c02b2_result_t CanIf_RecoverPump(void);          // tick 侧轮询
c02b2_result_t CanIf_RecoverChannel(flexcan_instance_t ch);
```

### 6.2 业务层 API（can_db.h）

```c
/* 按 DBC ID 分发 */
c02b2_result_t CanDb_DispatchByDb(u32 can_id, const u8 *payload, u8 len);

/* 单信号编解码 */
c02b2_result_t CanDb_DecodeSignal(signal_id_t sig, u64 raw, double *phys);
c02b2_result_t CanDb_EncodeSignal(signal_id_t sig, double phys, u64 *raw);
```

---

## 7. 变更操作

| 变更 | 操作路径 |
|---|---|
| 加 signal / 加报文 / 删报文 | docs/DBC_CHANGE_GUIDE.md § 3 全流程 |
| 改 timeout 表 | app/can/can_rx.c g_can_rx_timeout_table[]（手维护，AUTOGEN 标记已撤） |
| 改发送周期表 | app/can/can_tx.c g_can_tx_cycle_table[]（手维护） |
| 加新 TX 报文逻辑 | app/can/can_tx.c prv_rebuild_<msgname>() 加函数 + CanTx_Tick() 加调度 |
| 改 RX-FIFO 尺寸 | 不要改！修改 CAN_RX_FILTER_ELEMS_* 后 #error 会触发 |
| 改 SPSC ring 大小 | CAN_RX_RING_SIZE 改值后必须重测 64-ID 满载 case |

---

## 8. 验证清单（每次修改 CAN 必跑）

- [ ] IAR V9.40.1 编译 0 错 0 警
- [ ] `bash tools/check.sh` 通过
- [ ] `python tests/test_can_ipk.py` 全部通过（round-trip + boundary + cross-byte）
- [ ] CAN 分析仪实测：
  - [ ] public CAN 总线（500k 80%）TX 周期报正常
  - [ ] private CAN 总线 RX 触发分发回调
  - [ ] 总线关闭恢复流程（拔线 -> 插入 -> 自动恢复）

---

## 9. 相关 REVIEW 决议

详见 docs/REVIEW_NOTES.md：
- A1 SPSC ring __DMB() 屏障硬化
- A2 CanIf_Init() 重入契约
- A5 CanIf_Send 日志去重
- A10 / B7 RX filter 表尺寸 #error 锁死
- C6 Pa082 规避（volatile 一次访问）