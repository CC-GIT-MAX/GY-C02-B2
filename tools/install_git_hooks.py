#!/usr/bin/env python3
"""Configure this repository to use its versioned Git hooks."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path


def main() -> int:
    root_result = subprocess.run(
        ["git", "rev-parse", "--show-toplevel"],
        text=True,
        capture_output=True,
    )
    if root_result.returncode != 0:
        print("Not inside a Git repository.", file=sys.stderr)
        return 2

    root = Path(root_result.stdout.strip())
    hooks = root / ".githooks"
    hook = hooks / "pre-commit"
    if not hook.is_file():
        print(f"Missing hook: {hook}", file=sys.stderr)
        return 2

    completed = subprocess.run(
        ["git", "config", "--local", "core.hooksPath", ".githooks"],
        cwd=root,
        text=True,
        capture_output=True,
    )
    if completed.returncode != 0:
        print(completed.stderr.rstrip(), file=sys.stderr)
        return completed.returncode

    print("Enabled repository hooks from .githooks.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
