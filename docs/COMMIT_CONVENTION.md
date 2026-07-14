# 提交信息规范（Conventional Commits 简化版）

## 格式

```
<type>(<scope>): <subject>

<body>

<footer>
```

- **type**（必须）：`feat / fix / refactor / docs / test / build / ci / chore / perf / style`
- **scope**（可选）：模块名，如 `signal / can / storage / scheduler / main`
- **subject**（必须）：中文，30 字以内，动词开头，不要句号
- **body**（可选）：列出主要改动点，每行 `-` 开头
- **footer**（可选）：`Refs: <文档>`、`BREAKING CHANGE: <说明>`

## type 含义

| type | 含义 | 示例 |
|---|---|---|
| feat | 新功能 | `feat(can): add can_if.c skeleton` |
| fix | 修 bug | `fix(signal): fix invalid id range check` |
| refactor | 重构（无新功能、无 bug 修复） | `refactor(scheduler): split run loop` |
| docs | 仅文档 | `docs: add SIGNAL_GUIDE.md` |
| test | 仅测试 | `test(signal): add unit test stub` |
| build | 构建系统 | `build: bump linker script` |
| ci | CI 配置 | `ci: add code-style job` |
| chore | 杂项 | `chore: update .gitignore` |
| perf | 性能优化 | `perf(can): batch tx buffer` |
| style | 仅格式 | `style: reformat with clang-format` |

## 多提交一次 push 的写法

```
docs+chore(batch1): architecture, version template, clang-format

- docs/ARCHITECTURE.md: 项目宪章
- app/version.h.in: 版本宏模板
- .clang-format: 4 空格 + 120 列

Refs: docs/ARCHITECTURE.md
```

## 禁用

- ❌ `update` / `misc` / `tmp` 等模糊 type
- ❌ subject 以"我"/"我们"开头
- ❌ 多件事合一个 commit（拆开）


## Doxygen 钩子指令

`pre-commit` 钩子（`tools/codex_doxygen_staged.py`）默认是**旁观者**：
普通 `git commit -m "..."`（提交信息里没有下面任意指令、也没有设置下面任意环境变量）
不会触发 Codex CLI。

要让钩子真正去让 Codex 补 / 改 Doxygen 注释，在提交信息里加对应指令，
或在执行 `git commit` 之前设置环境变量：

| 触发方式                              | 行为                                                 |
| ------------------------------------- | ---------------------------------------------------- |
| `git commit -m "feat: ..."` （默认）   | **不译**，钩子直接放行，跳过 Codex                   |
| `[translate]` 指令                     | 仅补齐缺失 / 不合规的 Doxygen 块，保留已合规块       |
| `[retrans]` 指令                      | 重写所有 staged 文件的 Doxygen 块，包括已合规块      |
| `CODEX_DOXYGEN_SKIP=1` env             | 等价 native commit（强制跳过）                       |
| `CODEX_DOXYGEN_FORCE=1` env           | 等价 `[retrans]`                                      |
| `CODEX_DOXYGEN_DRY_RUN=1` env         | 只打印 prompt，不调用 Codex（调试用）                |

指令放在提交信息的 **subject 或 body 任意位置**都可，大小写不限，前缀后缀允许多余空格：

```
feat(can): 实现 CAN 接收中断 [translate]
feat(can): 重写所有 Doxygen 块 [retrans]
```

优先级：`SKIP` > `FORCE` > `DRY_RUN` > 指令 > 默认 SKIP。
即：`CODEX_DOXYGEN_SKIP=1 git commit -m "feat: [retrans] x"` 仍然跳过。

style 与具体规则参见 `docs/DOXYGEN_STYLE.md`。

