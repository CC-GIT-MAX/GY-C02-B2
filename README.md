# C02-B2 仪表 MCU 项目

## 简介

基于 YTM32B1MD1 的汽车仪表 MCU 软件。裸机 + RTI 时间片调度，IAR 9.x 编译。

## 目录结构

```
app/        业务模块入口（main.c / scheduler / signal / log / 各业务模块）
board/      板级配置（时钟 / 引脚 / 各外设 config）
middleware/ 通用中间件（printf / osif）
platform/   厂商 SDK（devices / drivers）
rtos/       OSIF 适配
EWARM/      IAR 工程文件
docs/       架构与设计文档
```

## 构建

使用 IAR Embedded Workbench 打开 `EWARM/C02_B2.eww`，选择 FLASH 配置，Rebuild All。

## 优化项实施记录

参见 `docs/OPTIMIZATION_PLAN.md` 与各 commit 历史。

## 自动化：Doxygen 注释在提交前自动补齐

`git commit` 时，仓库内的 `.githooks/pre-commit` 会调用 Codex CLI，为暂存区中
受维护的 `.c`/`.h` 文件补齐符合 [`docs/DOXYGEN_STYLE.md`](docs/DOXYGEN_STYLE.md)
的注释，并把结果重新加入暂存区。

启用方式（一次性，本地仓库配置）：

```bash
python tools/install_git_hooks.py
```

该命令等价于 `git config --local core.hooksPath .githooks`。此后所有
`git commit` 都会自动执行。脚本位于：

- `.githooks/pre-commit`：POSIX 兼容的 Git 钩子入口。
- `tools/codex_doxygen_staged.py`：筛选、安全检查、Codex 调用、校验、重暂存。
- `tools/install_git_hooks.py`：跨平台安装脚本。

行为说明：

- 只处理 `app/`、`tests/` 等受维护目录下的 `.c`/`.h` 文件；
  `CMSIS/`、`middleware/`、`platform/`、`rtos/`、`EWARM/`、`board/` 中的厂商代码
  与生成产物会被自动跳过。
- 若目标文件存在未暂存修改，钩子会中止提交，避免把工作区代码意外带入；
  提示先 `git add` 或 `git stash` 后重试。
- Codex 调用使用 `codex exec --ephemeral -s workspace-write` 沙箱，不绕过
  审批或沙箱，只在临时会话里运行，不会保存历史。

环境变量：

- `CODEX_DOXYGEN_SKIP=1`：跳过本次自动化（紧急提交用）。
- `CODEX_DOXYGEN_DRY_RUN=1`：仅打印会被处理的目标文件，不调用 Codex、不校验。
- `CODEX_DOXYGEN_FORCE=1`：忽略“已合规则跳过”逻辑，强制把每个目标文件都丢给 Codex 重新翻译（仅在你需要批量翻新注释时使用，日常提交不要开启）。
  钩子默认行为：调用 Codex 之前会逐文件检查 `file_is_already_compliant()`，已具备中文 `@brief` + 必需 `@param`/`@return` 的文件会被跳过，避免重复翻译。

失败处理：

- Codex 退出非零、`tools/check_doxygen.py` 报错或 `git add` 失败都会阻止提交，
  并在终端打印 stdout/stderr。请按提示修复后重新 `git commit`。
