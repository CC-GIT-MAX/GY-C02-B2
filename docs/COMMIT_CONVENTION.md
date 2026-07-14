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
