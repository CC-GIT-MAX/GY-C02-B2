# C02-B2 Review & Refactor Notes

> 跟踪表 + 决议。配合 `docs/ARCHITECTURE.md` 使用。
> 创建:`2026-07-12`,起点 commit `0110749`(`codex/home-signal`)。

## 0. 决议(锁定)

- **分支**:每 Phase 一个分支 `codex/refactor-phase-N`,依次 PR 进 `codex/home-signal`
- **DBC 工具链**:只新增 `tools/gen_ipk_runtime.py`,**不动** `tools/dbc_parse.py`
- **启动顺序**:同意调整 `main.c`(`RTI_Init` 提前到 `BSP_Init` 之后、`WDG_DRV_Init` 之前)
- **跟踪表**:本文件
- **审查**:每 Phase 跑 `tools/check.sh` + `tests/*.py`;IAR 编译由用户在本地跑

## 1. 基线(`0110749`)

`tools/check.sh` 输出(详见 `docs/review/phase0_check_baseline.txt`):

| 项 | 结果 |
|---|---|
| extern in app/**/*.c(excluding scheduler.c) | OK |
| driver headers in app/(except allowlist) | **FAIL: 1**(`app/drv_api/io/io.c:7` 引用 `pins_driver.h`) |
| raw printf() in app/ | OK |
| clang-format | SKIP(未安装) |

预存违规 1 处:`app/drv_api/io/io.c` 直接吃 `pins_driver.h`,违反 drv_api 分层。
**Phase 3 修**(`drv_api` 清理一并处理),Phase 0/1/2 不动。

## 2. 问题清单与阶段映射

### A. 正确性 / 稳定性

| ID | 位置 | 摘要 | Phase |
|---|---|---|---|
| A1 | `app/can/can_if.c` | SPSC ring 内存序未强化,跨核会破 | 2 |
| A2 | `app/can/can_if.c` | `CanIf_Init` 时序契约脆 | 4 |
| A3 | `app/can/can_rx.c` | 5/50ms 私有 slot 在 ISR/main 共享无屏障 | 2 |
| A4 | `app/can/can_tx.c` | `s_tx.send_lock` 占位无真锁 | 2 |
| A5 | `app/can/can_if.c` | `CanIf_Send` 错误日志未去重,bus-off 风暴刷屏 | 1 | `[x]` Phase 1 / A-2.5: `CanIf_Send()` 内已实现日志去重 + 冷却窗口 |
| A6 | `app/scheduler/scheduler.c` | 调度器无重入断言 | 1 | `[x]` Phase 1 / A-2.5: `Scheduler_Run()` 入口 `s_sched_depth` 自旋 + WDG 重启守卫 |
| A7 | `app/rti/rti.c` | `RTI_SlotElapsed` wrap-around 未注释 | 1 | `[x]` Phase 1 / A-2.5: rti.c @note 已说明 u32 wrap-around 语义 (`RTI_SlotElapsed` @note 段) |
| A8 | `app/can/can_if.c` | callback 安装时机无文档契约 | 1(只更文档) | `[x]` Phase 1 / A-2.5: `CanIf_InstallFlexcanCallbacks()` @details 明确"由 Can_Init() 内部调用一次" |
| A9 | `app/can/can_db_codec.c` | half-away-from-zero 负值边界 0/-1 抖动 | 3(随 codec 改) |
| A10 | `app/can/can_if.c` | `CAN_RX_FILTER_ELEMS_*` 硬编码 | 1 | `[x]` Phase 1 / A-2.5: 常量化 RFFN=8->72, RFFN=6->56, #error 锁死不变量 |

### B. 性能 / 资源

| ID | 位置 | 摘要 | Phase |
|---|---|---|---|
| B1 | `app/can/can_if.c` | RX ring `can_msg_t` 复制可换定长结构 | 2 |
| B2 | `app/scheduler/scheduler.c` | `prv_module_count` 每次重算 | 2(微优化) |
| B3 | `app/can/can_rx.c` | `prv_check_timeouts` 50ms 全表扫 | 2 |
| B4 | `app/can/can_tx.c` | `CanTx_RebuildFromSignals` 全量重建 | 2 |
| B5 | `app/signal/signal.c` | `Signal_InvalidateAll` int 循环 | 2(微优化) |
| B6 | `app/log/log.c` | 160B 栈 buf 截断无标记 | 1 | `[x]` Phase 1 / A-2.5: 缓冲 160->192B + 截断时追加 `~` 标记 |
| B7 | `app/can/can_if.c` | RFFN 启动两表 | 1(随 A10) | `[x]` Phase 1 / A-2.5: 随 A10 关闭 |

### C. 代码质量 / 可维护性

| ID | 位置 | 摘要 | Phase |
|---|---|---|---|
| C1 | `app/signal/signal.c` | 手写信号名表与 AUTOGEN 双源 | 3 |
| C2 | `app/drv_api/can/can_if.c` | drv_api 反向 include app/can | 3 |
| C3 | `app/can/can_rx.c` | `g_can_rx_timeout_table` 标 AUTOGEN 但实手维护 | 3 |
| C4 | `app/can/can_db.c` | `s_dbc_to_bus` 标生成但实手维护 | 3 |
| C5 | `app/scheduler/scheduler.c` | `g_sched_modules[]` + 5 extern 双重源 | 3 |
| C6 | `app/can/can_if.c` | volatile Pa082 workaround 跨工具链脆 | 2(随 A1) |
| C7 | `app/log/log.h` | `MOD_NAME` vs `LOG_NAME` 命名检查 | 1(只验证) | `[x]` Phase 1 / A-2.5: 21 .c 文件 `#define LOG_NAME` -> `#define MOD_NAME`; log.h 兼容 LOG_NAME 作遗留别名 |
| C8 | 跨模块 | commit emoji 风格 | 不动 |
| C9 | `app/can/can_rx.c` | 三数组三种语义无 `static_assert` | 1 | [x] Phase 1 / A-2.5
| C10 | `app/init/bsp_init.c` + `main.c` | WDG 在 RTI 之前使能 | 4 |

## 3. Phase 计划

| Phase | 主题 | 关键项 | 验收 |
|---|---|---|---|
| 0 | 基线(零行为变更) | 本表 + 基线 check 落档 + REVIEW 标记 | `check.sh` 输出与基线一致 |
| 1 | 稳定性必修 | A5 / A6 / A7 / A8 / A10 / B6 / B7 / C9 / C7 验证 | 4 项 check OK(允许 io.c 历史违规) |
| 2 | 性能与 ring 重构 | A1 / A3 / A4 / B1 / B2 / B3 / B4 / B5 / C6 | 同上 + 现有 demo sweep 不退化 |
| 3 | 架构层清理 | C1 / C2 / C3 / C4 / C5 + 新增 `tools/gen_ipk_runtime.py` + 修基线 1 处 io.c 违规 | 同上 + 工具链跑通 |
| 4 | 启动与重入稳健 | A2 / C10 + 测试 stub | 同上 + IAR 编译通过(用户跑) |
| 5 | 文档与契约收口 | `CAN_ARCHITECTURE.md` / `SCHEDULER_GUIDE.md` / 更新 `DBC_CHANGE_GUIDE.md` | 文档 + 检查脚本 |

## 4. 已知遗留(scope 外)

- `mod_power` / `mod_meter` / `app/fsm/` / `app/telltale/`(docs 里有 TODO)
- KV 仍是 skeleton
- commit emoji 风格
- `docs/CHANGELOG.md` 历史

## 5. REVIEW 标记约定

- 待重构代码上方加一行:`/* REVIEW(<Phase>): <id> <一行说明> */`
- 本表加状态:`[ ]` 待办 / `[~]` 进行中 / `[x]` 完成