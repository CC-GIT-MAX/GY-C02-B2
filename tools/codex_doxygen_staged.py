#!/usr/bin/env python3
"""Add DOXYGEN_STYLE comments to staged C/H changes through Codex CLI."""

from __future__ import annotations

import os
import re
import shutil
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

# CJK Unified Ideographs + full-width punctuation; matches tools/check_doxygen.py
CJK_RE = re.compile(r"[\u4e00-\u9fff\u3000-\u303f\uff00-\uffef]")
_PARAM_DIR_RE = re.compile(r"@param\s*(\[[^\]]+\])?")
_ZH_BRIEF_RE = re.compile(r"@brief\s+([^\n]+)")
_STORAGE_CLASS_RE = re.compile(r"\bstatic\b")
_FUNC_DEF_RE = re.compile(
    r"^[A-Za-z_][A-Za-z0-9_ *]*\s+[A-Za-z_][A-Za-z0-9_]*\s*\([^;{}]*\)\s*([;{]|$)"
)
_NAME_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*\s*\(")
_DECL_RE = re.compile(
    r"^([A-Za-z_][A-Za-z0-9_ *]+)\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(([^;{}]*)\)\s*;"
)


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
    style_hint = (
        "Strictly follow docs/DOXYGEN_STYLE.md. Comments MUST be Chinese-first "
        "(@brief zh required, @brief en optional; @details optional and recommended "
        "only in .c files; @param/@return follow the style guide)."
    )
    return (
        f"Update Doxygen comments for the staged C/H changes in this repository.\n\n"
        f"{style_hint}\n"
        f"Rules:\n"
        f"1. Read and strictly follow docs/DOXYGEN_STYLE.md.\n"
        f"2. Only modify the listed files.\n"
        f"3. Only add or correct Doxygen comments required by that style.\n"
        f"4. Do not change program behavior, declarations, function signatures, "
        f"formatting outside comments, or generated artifacts.\n"
        f"5. Preserve all existing staged code changes.\n"
        f"6. Do not run git add, git commit, git reset, git checkout, or any command "
        f"that changes the Git index.\n"
        f"7. Inspect the staged diff with git diff --cached -- for context, but edit "
        f"the working-tree files.\n"
        f"8. Do NOT rewrite Doxygen blocks that already have a Chinese @brief and "
        f"the required @param/@return tags - leave them untouched.\n"
        f"9. Finish after comments are compliant; do not modify unrelated code.\n\n"
        f"Allowed files:\n{file_list}\n"
    )


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


def resolve_codex_command() -> list[str]:
    """Build an argv list that Windows CreateProcess can launch.

    `codex` resolves to a `.CMD` shim under npm on Windows; Python subprocess
    cannot `CreateProcess` such a shim with a list argv (WinError 2/5). We
    therefore launch it through `cmd.exe /c`, which IS a real PE binary.
    On non-Windows the binary is invoked directly. An explicit CODEX_BIN
    environment variable lets the user point at a specific `codex` binary.
    """
    override = os.environ.get("CODEX_BIN", "").strip()
    if override:
        return [override]
    if sys.platform == "win32":
        return ["cmd.exe", "/c", "codex"]
    found = shutil.which("codex")
    return [found or "codex"]


def _block_above(lines: list[str], func_line_1based: int) -> str | None:
    """Return the Doxygen block immediately above func_line_1based, or None.

    Walks up from the function line, skipping blank lines, until it finds a
    line starting with `/**`. Every intervening line must be a comment or
    blank; the first non-comment, non-blank line we hit (e.g. `#include`,
    a code statement, a stray `/* ... */` block) means there is no
    doxygen block immediately above.
    """
    s = func_line_1based - 1
    # Skip blank lines directly above the function.
    while s >= 1 and not lines[s - 1].strip():
        s -= 1
    if s < 1:
        return None
    # The line above the function (after blanks) must end the doxygen block.
    if not lines[s - 1].strip().endswith("*/"):
        return None
    # Walk backwards through the doxygen block; record its start.
    end = s  # end is exclusive (one past the last line of the block)
    s -= 1
    while s >= 1:
        c = lines[s - 1]
        stripped = c.lstrip()
        if stripped.startswith("/**"):
            return "\n".join(lines[s - 1 : end])
        if stripped.startswith("*") or stripped.endswith("*/") or stripped == "":
            s -= 1
            continue
        if stripped.startswith("/*"):
            # Plain /* ... */ (not /**): treat as no doxygen block.
            return None
        # Anything else (code, #include, etc.) means no block immediately above.
        return None
    return None


def file_is_already_compliant(path: Path, suffix: str = ".c") -> bool:
    """Return True iff every non-static function in `path` has a Doxygen block
    with a Chinese @brief, the right @param(s) when there are parameters, and
    a @return for non-void return types.

    Used to skip files that are already compliant (so we don't re-translate
    them on every commit). When in doubt, return False so Codex still gets
    a chance to fix it.
    """
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return False
    lines = text.splitlines()
    if not lines:
        return False

    is_c_source = suffix.lower() == ".c"
    saw_non_static = False
    for lineno, line in enumerate(lines, 1):
        if not _FUNC_DEF_RE.match(line):
            continue
        m = _NAME_RE.search(line)
        if not m:
            continue
        name = re.sub(r"\s*\($", "", m.group(0))
        if name == "main" or "typedef" in line:
            continue
        is_static = bool(_STORAGE_CLASS_RE.search(line))
        if is_c_source and is_static:
            continue
        saw_non_static = True
        block = _block_above(lines, lineno)
        if block is None or "@brief" not in block:
            return False
        if not any(CJK_RE.search(m.group(1)) for m in _ZH_BRIEF_RE.finditer(block)):
            return False
        # @param for each parameter?
        decl = _DECL_RE.match(line)
        if decl:
            params = decl.group(3).strip()
            if params and params != "void":
                if not _PARAM_DIR_RE.search(block):
                    return False
        else:
            if "(" in line:
                args = line.split("(", 1)[1].split(")", 1)[0]
                if args.strip() and args.strip() != "void":
                    if not _PARAM_DIR_RE.search(block):
                        return False
        # @return for non-void?
        if "(" in line:
            tokens = line.split("(", 1)[0].strip().split()
            ret_token = tokens[-2] if len(tokens) >= 2 else ""
        else:
            ret_token = ""
        if ret_token and ret_token != "void" and "@return" not in block:
            return False
    if not saw_non_static:
        return False
    return True


def _filter_compliant_targets(targets: Sequence[str], root: str) -> list[str]:
    """Drop targets whose working-tree files are already compliant."""
    remaining: list[str] = []
    for rel in targets:
        abs_path = Path(root) / rel.replace("/", os.sep)
        suffix = Path(rel).suffix.lower()
        if file_is_already_compliant(abs_path, suffix=suffix):
            print(
                f"[codex-doxygen] Skipping (already compliant): {rel}",
                file=sys.stderr,
            )
            continue
        remaining.append(rel)
    return remaining


def run_automation(
    runner: Runner = subprocess.run,
    environ: Mapping[str, str] | None = None,
) -> int:
    environment = os.environ if environ is None else environ
    if is_enabled(environment, "CODEX_DOXYGEN_SKIP"):
        print("[codex-doxygen] Skipped by CODEX_DOXYGEN_SKIP.", file=sys.stderr)
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
        print("[codex-doxygen] No staged maintained C/H files; nothing to do.", file=sys.stderr)
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

    print("[codex-doxygen] Targets:", file=sys.stderr)
    for path in targets:
        print(f"  - {path}", file=sys.stderr)

    force = is_enabled(environment, "CODEX_DOXYGEN_FORCE")
    if not force:
        targets = _filter_compliant_targets(targets, root)
        if not targets:
            print(
                "[codex-doxygen] All targets are already compliant; nothing to do.",
                file=sys.stderr,
            )
            return 0

    if is_enabled(environment, "CODEX_DOXYGEN_DRY_RUN"):
        print("[codex-doxygen] Dry run; Codex was not invoked.", file=sys.stderr)
        return 0

    prompt = build_prompt(targets)
    codex_result = run_command(
        runner,
        [*resolve_codex_command(), "exec", "--ephemeral", "-s", "workspace-write", "-C", root, prompt],
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

    print("[codex-doxygen] Comments validated and staged.", file=sys.stderr)
    return 0


def main() -> int:
    return run_automation()


if __name__ == "__main__":
    raise SystemExit(main())
