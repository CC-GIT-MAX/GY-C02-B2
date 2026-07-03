# 优化项实施记录

| 批次 | 主题 | 状态 | Commit |
|---|---|---|---|
| 0 | 本地仓库初始化 + 业务骨架（scheduler/signal/log/rti/result + mod_template + main.c 改写） | ✅ | `chore: init local repo` / `feat(app): scaffold scheduler/signal/log/rti/result + mod_template` |
| 1 | 项目元信息（ARCHITECTURE/version.h.in/clang-format/.gitignore 扩展） | ✅ | `docs+chore(batch1): architecture, version template, clang-format, .gitignore expansion` |
| 2 | app/types.h 别名层 | ✅ | `feat(app): types.h alias layer (batch2)` |
| 3 | 信号中心扩展 + 单元测试桩 + SIGNAL_GUIDE.md | ✅ | `feat(signal)+docs(batch3): expand signal bus, add unit test stub, signal guide` |
| 4 | CAN / 诊断 / 存储中间件骨架 | ✅ | `feat(can/diag/storage): middleware skeletons (batch4)` |
| 5 | CI / 静态扫描 / 提交规范 | ✅ | `ci+docs+tools(batch5): CI pipeline, pre-commit check, commit convention` |

## 当前架构状态

- **调度**：超级循环 + RTI 1ms 时间片（`app/scheduler` + `app/rti`）
- **业务接入**：3 步添加新模块（实现 mod_desc_t → 注册到 g_modules → 不动 main.c）
- **跨模块通信**：信号总线（`app/signal/signal.h`），单一所有者原则
- **错误处理**：`lbx_result_t`（`app/result.h`），分段错误码
- **日志**：4 级 LOG_E/W/I/D，模块名标签，编译期零开销
- **中间件骨架**（待填实现）：
  - CAN：`can_if`（接口）+ `can_db`（报文表）
  - 诊断：`diag_if`（UDS 入口）
  - 存储：`kv`（KV 抽象 + 提交语义）
- **质量门禁**：
  - `tools/check.sh` 本地与 CI 共用（extern/驱动头/raw printf）
  - `.clang-format` 自动格式
  - `docs/COMMIT_CONVENTION.md` 提交规范

## 下一步建议（待业务推进时按需执行）

1. 业务模块首个落地产物（建议 `mod_power` 或 `mod_can_rx`）
2. CAN 报文数据库填充（`app/can/can_db.c`）
3. `version.h` 注入脚本（CI `version-job`）
4. 状态机框架（`app/fsm/`）
5. 指示灯查表（`app/telltale/`，参考 LBXA00 旧项目 TEL_LOGIC）
