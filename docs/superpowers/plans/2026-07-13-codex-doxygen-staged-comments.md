# Codex Doxygen Staged Comments Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a shared pre-commit automation that invokes Codex CLI to add DOXYGEN_STYLE-compliant comments to staged C/H changes.

**Architecture:** A versioned Git hook delegates to a testable Python command. The command derives its target set from the Git index, protects unstaged work, invokes Codex with a constrained prompt, validates the result, and stages only the original targets.

**Tech Stack:** Python 3 standard library, Git hooks, Codex CLI, unittest.

---

### Task 1: Staged-file selection and safeguards

**Files:**
- Create: `tests/test_codex_doxygen_staged.py`
- Create: `tools/codex_doxygen_staged.py`

- [ ] Write selection tests for maintained `.c`/`.h`, excluded SDK paths, and unstaged conflicts.
- [ ] Run `python -m unittest tests.test_codex_doxygen_staged -v`; expect import failure.
- [ ] Implement Git helpers, normalized filtering, exclusions, and unstaged-change detection.
- [ ] Re-run the focused tests; expect selection and safeguard tests to pass.

### Task 2: Codex invocation and restaging

**Files:**
- Modify: `tests/test_codex_doxygen_staged.py`
- Modify: `tools/codex_doxygen_staged.py`

- [ ] Add orchestration tests for no-op, dry-run, Codex arguments, validation, and restaging.
- [ ] Run focused tests; expect orchestration failures.
- [ ] Build a constrained prompt and invoke `codex exec --ephemeral -s workspace-write -C <root> -`.
- [ ] Run `tools/check_doxygen.py` for targets and `git add -- <targets>` only after success.
- [ ] Re-run focused tests; expect all tests to pass.

### Task 3: Hook installation

**Files:**
- Create: `.githooks/pre-commit`
- Create: `tools/install_git_hooks.py`
- Modify: `tests/test_codex_doxygen_staged.py`

- [ ] Add a temporary-repository installer test; expect failure before implementation.
- [ ] Add a POSIX hook that delegates to Python.
- [ ] Add installer verification and `git config --local core.hooksPath .githooks`.
- [ ] Run tests and then `python tools/install_git_hooks.py`; expect success.

### Task 4: Documentation and verification

**Files:**
- Modify: `README.md`

- [ ] Document prerequisites, installation, normal behavior, skip, dry-run, and safeguards.
- [ ] Run dry-run against the repository; expect a clean no-op or target list.
- [ ] Run `python -m unittest discover -s tests -v`; expect all tests to pass.
- [ ] Run `git diff --check` and inspect status/diff; expect only scoped changes.
