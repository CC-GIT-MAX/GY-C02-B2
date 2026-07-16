# 提交信息规范（Conventional Commits 简化版）

## 格式

```
<type>(<scope>): <subject>

<body>

<footer>
```

- **type**（必须）：`feat / fix / refactor / docs / test / build / ci / chore / perf / style`
- **scope**（可选）：模块名 / 文档子目录，如 `signal / can / storage / doxygen_style / sdd_power`
- **subject**（必须）：**中文，30 字以内，动词开头，不要句号**
- **body**（可选）：**每行以 `-` 开头**，列出主要改动点（一行一条）；空行分隔段落
- **body 行宽**：每行 < 100 字符（中文按 1 char 计）
- **整段限制**：**subject + body + footer 总字符数 < 300**（设计/重构型可附 ≤ 600，
  但 body 第一条必须写"动机背景"；详见 §subject 写法 §3）
- **footer**（可选）：`Refs: <文档>`、`BREAKING CHANGE: <说明>`

## type 含义

| type | 含义 | 单一职责 | 示例 |
|---|---|---|---|
| feat | 新功能 | 仅新 API / 新模块 | `feat(can): 新增 can_if.c 骨架` |
| fix | 修 bug | 仅 bug 修复 | `fix(signal): 修 id 越界判空` |
| refactor | 重构（无新功能、无 bug 修复） | 仅结构变更 | `refactor(scheduler): 拆分 run loop` |
| docs | 仅文档 | **不允许改运行时代码** | `docs: 新增 SIGNAL_GUIDE.md` |
| test | 仅测试 | 仅 test/ 目录或 *_test.c | `test(signal): 新增单元测试桩` |
| build | 构建系统 | 仅 Makefile / linker / IAR 工程 | `build: 升级 linker 脚本` |
| ci | CI 配置 | 仅 .github/ / tools/check_* | `ci: 新增代码风格 job` |
| chore | 杂项 | 工具脚本 / .gitignore / 不改 runtime | `chore: 升级 .gitignore` |
| perf | 性能优化 | 性能可量化 | `perf(can): 批量 TX buffer` |
| style | 仅格式 / 注释 | **不动代码逻辑** | `style(signal): 按规范重写注释` |

### 跨类型混合的处理

> 同一个 commit 同时改了文档 + 运行时代码（例如本次 0d16c29），**禁止**用 `docs`，
> 应用 `style`（仅注释）或 `refactor`（也含注释），并在 body 第一条明确写
> "本次变更仅涉及注释重写，逻辑未改"或指明功能调整点。
>
> 涉及 `feat + docs + refactor` 三个独立动机的，**必须拆 commit**。

## subject 写法（中文 30 字以内）

| ✅ 正确 | ❌ 错误 |
|---|---|
| `重构总线为 v0.5 F+B+Ever 模型` (15 字) | `refactor(signal): v0.5 F+B+Ever model — drop per-slot valid/ever_set` (英文) |
| `删除英文 @brief；行宽改 < 85` (12 字) | `docs(DOXYGEN): drop English @brief; line width < 85; tighten CI checks` (英文 + 超长) |
| `修复 timeout 位图置 1 越界` (10 字) | `fix(can_rx): timeout bitmap 职责分离 — 置1在prv_check_timeouts, 置0在prv_drain` (英文 + 超长) |
| `prv_standby 改写超时位图回退` (13 字) | `feat(can): 写超时位图复位逻辑 (方案 B)` (超长 + "我们"不必要) |

**强制规则**：
- ❌ 禁用英文 subject（含专有名词例外，如 `v0.5`、`KEEP_LAST`、`prv_standby`）
- ❌ **禁止使用会话内部的代号 / 缩写**（`F+B+Ever`、`KISS`、`DDD`、`TBD`、`v0.x→v0.y`、
  本次会话独有的 `方案 A/B/C` 等）。如果来源是某方案/计划，要么写出该方案的具体改动描述
  （"重构总线为 bitmap 派生有效位 + 新增 ever-received bitmap"），要么在 body 第一条
  引述出处（`Refs: docs/<plan>.md`）
- ❌ 禁止以"我"/"我们"/"添加"/"修改"等冗余动词开头（推荐"新增"/"修复"/"重构"/"删除"）
- ❌ 禁止超过 30 字符
- ❌ 禁止句号结尾
- ❌ **整段描述（subject + body + footer）总字符 < 300**（修改 > 10 文件或新增 > 5 文件
  的允许 ≤ 600，但 body 必须先写一段 < 100 字符的动机说明，让读者无需看聊天记录就能
  理解为什么要改）
- ✅ 主体用中文；专有名词 `DBC`、`KEEP_LAST`、`prv_standby`、`GenSigStartValue` 保留半角

## body 写法（每行 `-` 开头）

```
refactor(signal): 重构总线为 v0.5 F+B+Ever 模型

- 删除 per-slot valid / ever_set，validity 改由 timeout bitmap 派生
- 新增 SIG_CAN_RX_EVER_RECEIVED_{LO,HI,HI2} bitmap
- 新增 Signal_HasEverReceived / Signal_ResetBootDone 等 4 个 API
- 删除 Signal_GetStored / Signal_Invalidate / Signal_InvalidateAll
- Signal_Reset 改为写 DBC init_value
- CanDb_SigToTimeoutBit 反向查表
- 新增 INIT_DBC / KEEP_LAST signal-level policy
- can_rx.c 5 个 prv_* 函数适配新模型
- 文档 ARCHITECTURE / SIGNAL_GUIDE / CHANGELOG 同步 v0.5

Refs: docs/SIGNAL_GUIDE.md, docs/ARCHITECTURE.md
```

**规则**：
- ✅ 第一行 paragraph（可空）说明动机 / 上下文（**0~2 行**）
- ✅ 之后每行 `- ` 开头，一行一条改动点
- ✅ 行宽 < 100 字符
- ✅ 改动点用中文动词开头（"新增"、"删除"、"修复"、"改为"、"提取"、"补齐"）
- ❌ 禁止 paragraph 内嵌入代码块（除非必要）
- ❌ 禁止带数字编号（`1.`/`2.`），全部用 `-`

## 多提交一次 push 的写法（info-only）

> **v0.3 时期用法已废止**——以前允许一批改动合一个 commit 并按 `1. 2. 3.` 编号，
> 新规则下**强制逐条 `- ` 列表**。如信息确实需要分组，用**空行分隔**多个 `- ` 段。

## footer

| 写法 | 触发时机 |
|---|---|
| `Refs: docs/SDD_POWER.md` | 引用的设计文档 |
| `Refs: #123` | GitHub / GitLab issue 号 |
| `BREAKING CHANGE: <说明>` | 公共 API 删除 / 行为变更 |
| `Co-authored-by: name <mail>` | 多人共编 |

## 禁用

- ❌ `update` / `misc` / `tmp` / `cleanup` 等模糊 type
- ❌ 英文 subject
- ❌ **会话内部代号 / 缩写**（无引用即无含义的 `F+B+Ever` 等）
- ❌ subject 以"我"/"我们"/"添加"/"修改"等冗余动词开头（推荐"新增"/"修复"/"重构"/"删除"）
- ❌ 多件独立动机合一个 commit（拆开；feat+docs 必须拆）
- ❌ body 段落以叙述句开头而不带 `- `
- ❌ body 行 > 100 字符
- ❌ 整段描述 > 300 字符（除非符合"动机说明"豁免）
- ❌ 用句号（中文 / 英文）结尾

## 历史 commit 不适用此规范

> 本规范**自 2026-07-16 起生效**。更早的 commit 描述（含合并分支中带入的
> 旧描述）允许不遵守下列条款：
>
> - 不强制要求 subject 中文化 / ≤ 30 字符
> - 不强制 body `- ` 列表 / < 100 字符 / 总段 < 300
> - 不禁止 `F+B+Ever` 等会话内部代号
> - 不要求 `Refs:` 出处
>
> 检查脚本默认只对**新** commit 生效（`commit-msg` hook 与 CI 的 latest 提交校验）。
> 若需对历史 commit 做"事后整改"，必须用 `git commit --amend` 或交互式 rebase
> **逐个**重写，**禁止**批量 `rebase --root` / `filter-branch` / `filter-repo`
> —— 那会改写所有人的本地提交哈希，引发协作冲突。
>
> 历史不合规不必视为 bug，请按当前规范提交下一条 commit 即可。

## CI 自动化检查

`tools/check_commit_msg.sh` 在 pre-commit hook 与 GitHub Actions 调用，至少覆盖：

- type 在白名单内（`feat|fix|refactor|docs|test|build|ci|chore|perf|style`）
- subject 全部 UTF-8 中文字符（允许专有名词半角）
- subject ≤ 30 字符
- body 若非空，至少有一行以 `- ` 开头
- body 每行 ≤ 100 字符
- **整段描述（subject + 空行 + body + 空行 + footer）总字符 ≤ 300**
  - 例外：修改 > 10 文件或新增 > 5 文件允许 ≤ 600，但 body 第一段必须是 < 100 字符的动机说明
- **代号检测**：subject 命中 `(F\+B\+Ever|KISS|DDD|TBD|RFC)` 或最近 7 天 chat 上下文里
  独有的 `方案 [A-Z] / PATH [A-Z]` 时拒绝（必须有 `Refs: <doc>` 出处）

```
# 启用方式
ln -sf "$(pwd)/tools/check_commit_msg.sh" .git/hooks/commit-msg
# 或在 GitHub Actions lint-job 调用：
- run: bash tools/check_commit_msg.sh < commit_msg_file
```
