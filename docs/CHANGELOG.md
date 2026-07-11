# 优化项实施记录

| 批次 | 主题 | 状态 | Commit |
|---|---|---|---|
| 0 | 本地仓库初始化 + 业务骨架（scheduler/signal/log/rti/result + mod_template + main.c 改写） | ✅ | `chore: init local repo` / `feat(app): scaffold scheduler/signal/log/rti/result + mod_template` |
| 1 | 项目元信息（ARCHITECTURE/version.h.in/clang-format/.gitignore 扩展） | ✅ | `docs+chore(batch1): architecture, version template, clang-format, .gitignore expansion` |
| 2 | app/types.h 别名层 | ✅ | `feat(app): types.h alias layer (batch2)` |
| 3 | 信号中心扩展 + 单元测试桩 + SIGNAL_GUIDE.md | ✅ | `feat(signal)+docs(batch3): expand signal bus, add unit test stub, signal guide` |
| 4 | CAN / 诊断 / 存储中间件骨架 | ✅ | `feat(can/diag/storage): middleware skeletons (batch4)` |
| 5 | CI / 静态扫描 / 提交规范 | ✅ | `ci+docs+tools(batch5): CI pipeline, pre-commit check, commit convention` |
| 6 | Doxygen 注释规范 + 自动化检查 + SDD 模板 + 架构图 | ✅ | docs(tooling): doxygen style + check_doxygen + SDD template + arch diagram |
| 7 | LBX->C02B2 重命名 + diag/meter/power 清理 + 完整 IAR 本地编译 + 文档同步 | ✅ | `99fa39a` / `0a263d1` / `a4f92c8` |

## 当前架构状态

- **调度**：超级循环 + RTI 1ms 时间片（`app/scheduler` + `app/rti`）
- **业务接入**：4 步添加新模块（5 钩子 mod_desc_t → 顶部 extern + g_sched_modules → SCHED_REGISTER → 加进 IAR 工程；不动 main.c）
- **跨模块通信**：信号总线（`app/signal/signal.h`），单一所有者原则
- **错误处理**：`c02b2_result_t`（`app/result.h`），分段错误码（含 `C02B2_ERR_NOT_FOUND`）
- **日志**：4 级 LOG_E/W/I/D，模块名标签，编译期零开销
- **中间件（已落地 10 RX + 3 TX，132 signals round-trip + boundary 全部通过）**：
  - CAN：`can_if`（接口，5ms drain ring + DBC 解码）+ `can_db`（IPK 报文表 + codec + dispatch facade）+ `can_rx`（drain + dispatch + 50ms 超时）+ `can_tx`（encode + trigger）
  - 存储：`kv`（KV 抽象 + 提交语义，预留接口）
  - 诊断：暂未实现（占位见 `docs/SDD_POWER.md`）
- **质量门禁**：
  - `tools/check.sh` 本地与 CI 共用（extern/驱动头/raw printf）
  - `.clang-format` 自动格式
  - `docs/COMMIT_CONVENTION.md` 提交规范

## 下一步建议（待业务推进时按需执行）

1. ✅ 已完成：IPK 64 RX + 9 TX（含超时 + Sentinel + factor_cache）
2. ✅ 已完成：`mod_can_demo` / `mod_rti_demo`（编译开关，待调试接分析仪）
3. 业务模块落地：`mod_power`（KL30/IGN/低功耗，草案见 `docs/SDD_POWER.md`），`mod_meter`（指针/告警灯）
4. `version.h` 注入脚本（CI `version-job`）
5. 状态机框架（`app/fsm/`）
6. 指示灯查表（`app/telltale/`，参考历史项目 TEL_LOGIC）
