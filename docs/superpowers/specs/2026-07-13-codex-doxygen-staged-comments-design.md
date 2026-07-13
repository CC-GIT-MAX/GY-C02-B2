# Codex Doxygen Staged Comments Design

## Goal

在提交前自动调用 Codex CLI，为暂存区内受维护的 C 语言源文件补齐符合
`docs/DOXYGEN_STYLE.md` 的 Doxygen 注释，并把生成的注释重新加入暂存区。

## Trigger

仓库使用可版本控制的 `.githooks/pre-commit`。安装脚本将本地 Git 配置
`core.hooksPath` 设置为 `.githooks`，因此执行 `git commit` 时自动触发。
Git 本身没有“文件加入暂存区后”事件，pre-commit 是最接近且可靠的自动触发点。

## Processing Flow

1. 读取 `git diff --cached --name-only --diff-filter=ACMR`。
2. 仅保留 `.c` 和 `.h` 文件，并排除厂商 SDK、第三方及生成代码目录。
3. 若没有目标文件，立即成功退出。
4. 将目标文件清单和严格约束传给 `codex exec`：只允许修改列出的文件，只添加或修正
   Doxygen 注释，不得更改程序行为，并必须遵守 `docs/DOXYGEN_STYLE.md`。
5. Codex 返回后运行 Doxygen 检查器校验目标文件。
6. 校验成功后执行 `git add -- <目标文件>`，把注释更新到暂存区；失败则阻止提交。

## Safety

- 默认使用 Codex workspace-write 沙箱，不绕过审批或沙箱。
- 自动化只处理调用前已暂存的 `.c`/`.h` 文件。
- 若目标文件存在未暂存修改，hook 拒绝执行，避免把用户未暂存的代码意外加入提交。
- `CODEX_DOXYGEN_SKIP=1` 可临时跳过自动注释。
- `CODEX_DOXYGEN_DRY_RUN=1` 只打印目标文件，不调用 Codex。
- Codex 不可用、退出失败或校验失败时阻止提交并输出修复提示。

## Repository Files

- `.githooks/pre-commit`：轻量入口，转发到 Python 自动化脚本。
- `tools/codex_doxygen_staged.py`：筛选、保护、Codex 调用、校验、重暂存。
- `tools/install_git_hooks.py`：跨平台配置 `core.hooksPath`。
- `tests/test_codex_doxygen_staged.py`：覆盖筛选、安全检查、命令构造及 dry-run。
- `README.md`：安装、跳过和故障排查说明。

## Acceptance Criteria

- 暂存区没有 C/H 文件时不调用 Codex。
- 目标文件含未暂存变更时不调用 Codex且提交失败。
- Codex 提示词明确引用并强制遵守 `docs/DOXYGEN_STYLE.md`。
- Codex 成功且检查通过时，修改后的目标文件被重新暂存。
- 自动化文件可被版本控制并通过单条安装命令启用。
