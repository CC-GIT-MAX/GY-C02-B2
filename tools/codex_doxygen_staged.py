#!/usr/bin/env python3
"""Add DOXYGEN_STYLE comments to staged C/H changes through Codex CLI."""

from __future__ import annotations

import os
import subprocess
import sys
from collections.abc import Mapping, Sequence
from pathlib import Path, PurePosixPath
from typing import Any, Callable

EXCLUDED_PREFIXES = (
    "cmsis/",
    "middleware/",
    "platform/",
    "rtos/",
    "ewarm/",
    "board/",
)
TRUE_VALUES = {"1", "true", "yes", "on"}
Runner = Callable[..., subprocess.CompletedProcess[str]]


def python_executable() -> str:
    return sys.executable


def normalize_path(path: str) -> str:
    return path.replace("\\", "/").lstrip("./")


def select_targets(staged_paths: Sequence[str]) -> list[str]:
    targets: list[str] = []
    for raw_path in staged_paths:
        path = normalize_path(raw_path.strip())
        lower_path = path.lower()
        if not path or PurePosixPath(path).suffix.lower() not in {".c", ".h"}:
            continue
        if lower_path.startswith(EXCLUDED_PREFIXES):
            continue
        targets.append(path)
    return targets


def find_unstaged_targets(targets: Sequence[str], unstaged_paths: Sequence[str]) -> list[str]:
    unstaged = {normalize_path(path).casefold() for path in unstaged_paths if path.strip()}
    return [path for path in targets if normalize_path(path).casefold() in unstaged]


def build_prompt(targets: Sequence[str]) -> str:
    file_list = "\n".join(f"- {path}" for path in targets)
    return f"""Update Doxygen comments for the staged C/H changes in this repository.

Mandatory rules:
1. Read and strictly follow docs/DOXYGEN_STYLE.md.
2. Only modify the listed files.
3. Only add or correct Doxygen comments required by that style.
4. Do not change program behavior, declarations, function signatures, formatting outside comments, or generated artifacts.
5. Preserve all existing staged code changes.
6. Do not run git add, git commit, git reset, git checkout, or any command that changes the Git index.
7. Inspect the staged diff with git diff --cached -- for context, but edit the working-tree files.
8. Finish after comments are compliant; do not modify unrelated code.

Allowed files:
{file_list}
"""


def is_enabled(environ: Mapping[str, str], name: str) -> bool:
    return environ.get(name, "").strip().lower() in TRUE_VALUES


def run_command(runner: Runner, command: list[str], **kwargs: Any) -> subprocess.CompletedProcess[str]:
    return runner(command, text=True, capture_output=True, **kwargs)


def output_lines(completed: subprocess.CompletedProcess[str]) -> list[str]:
    return [line.strip() for line in completed.stdout.splitlines() if line.strip()]


def print_failure(label: str, completed: subprocess.CompletedProcess[str]) -> None:
    print(f"[codex-doxygen] {label} failed.", file=sys.stderr)
    if completed.stdout.strip():
        print(completed.stdout.rstrip(), file=sys.stderr)
    if completed.stderr.strip():
        print(completed.stderr.rstrip(), file=sys.stderr)


def run_automation(
    runner: Runner = subprocess.run,
    environ: Mapping[str, str] | None = None,
) -> int:
    environment = os.environ if environ is None else environ
    if is_enabled(environment, "CODEX_DOXYGEN_SKIP"):
        print("[codex-doxygen] Skipped by CODEX_DOXYGEN_SKIP.")
        return 0

    root_result = run_command(runner, ["git", "rev-parse", "--show-toplevel"])
    if root_result.returncode != 0:
        print_failure("Repository discovery", root_result)
        return 2
    root = root_result.stdout.strip()

    staged_result = run_command(
        runner,
        ["git", "diff", "--cached", "--name-only", "--diff-filter=ACMR"],
        cwd=root,
    )
    if staged_result.returncode != 0:
        print_failure("Staged-file discovery", staged_result)
        return 2

    targets = select_targets(output_lines(staged_result))
    if not targets:
        print("[codex-doxygen] No staged maintained C/H files; nothing to do.")
        return 0

    unstaged_result = run_command(
        runner,
        ["git", "diff", "--name-only", "--", *targets],
        cwd=root,
    )
    if unstaged_result.returncode != 0:
        print_failure("Unstaged-change check", unstaged_result)
        return 2
    conflicts = find_unstaged_targets(targets, output_lines(unstaged_result))
    if conflicts:
        print("[codex-doxygen] Refusing to stage over unstaged changes:", file=sys.stderr)
        for path in conflicts:
            print(f"  - {path}", file=sys.stderr)
        print("Stage or stash those changes, then retry the commit.", file=sys.stderr)
        return 2

    print("[codex-doxygen] Targets:")
    for path in targets:
        print(f"  - {path}")
    if is_enabled(environment, "CODEX_DOXYGEN_DRY_RUN"):
        print("[codex-doxygen] Dry run; Codex was not invoked.")
        return 0

    prompt = build_prompt(targets)
    codex_result = run_command(
        runner,
        ["codex", "exec", "--ephemeral", "-s", "workspace-write", "-C", root, "-"],
        input=prompt,
        cwd=root,
    )
    if codex_result.returncode != 0:
        print_failure("Codex CLI", codex_result)
        return 2

    check_result = run_command(
        runner,
        [python_executable(), "tools/check_doxygen.py", *targets],
        cwd=root,
    )
    if check_result.returncode != 0:
        print_failure("Doxygen validation", check_result)
        return 2

    add_result = run_command(runner, ["git", "add", "--", *targets], cwd=root)
    if add_result.returncode != 0:
        print_failure("Restaging", add_result)
        return 2

    print("[codex-doxygen] Comments validated and staged.")
    return 0


def main() -> int:
    return run_automation()


if __name__ == "__main__":
    raise SystemExit(main())
