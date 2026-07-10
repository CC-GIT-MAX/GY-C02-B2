# IAR Local Build Guide

> Status: **tooling in place, awaiting IAR install on a build host**
> Last verified: 2026-07-06

This document explains how to take the C02-B2 firmware from "compiles in
my head" to "compiles in IAR EWARM 9.x on a real workstation", given the
project structure shown in `docs/ARCHITECTURE.md`.

## TL;DR

```bash
# 1. (host with IAR 9.30+) install once
#    default path: "C:/Program Files/IAR Systems/Embedded Workbench 9.40/"

# 2. (host with Python 3.9+) make sure ewp has every .c/.h
python tools/ewp_add_files.py --apply

# 3. (host with IAR) drive the build
tools/iar_build.sh --make        # 42 lines, bash + iarbuild
# or interactively:
#   IAR EWARM -> File -> Open Workspace -> EWARM/C02_B2.eww -> F7
```

## Why a script for the ewp?

Until commit `0a263d1`, the IAR project file `EWARM/C02_B2.ewp` only
listed `app/main.c` as a source. Every other business module
(`can/`, `init/`, `log/`, `rti/`, `scheduler/`, `signal/`, `storage/`,
`mod_template/`, plus the top-level `result.h` and `types.h`) was
invisible to the IAR build. The IDE happily reported "0 errors 0
warnings" while emitting a `C02_B2.out` that contained only the SDK
and an empty `main()`.

Manually re-adding 31 files via the IDE is error-prone and not
reviewable. `tools/ewp_add_files.py` does it idempotently with a
two-patch diff (one for the `<file>` entries, one for the
`CCIncludePath2` state), so every PR can re-run it and reviewers see
the exact change.

### What it touches

| Section | What changes |
|---|---|
| `<group><name>app</name>` | Insert 8 child `<group>` blocks (`can`, `init`, `log`, `rti`, `scheduler`, `signal`, `storage`, `mod_template`) plus 2 top-level `<file>` entries (`app/result.h`, `app/types.h`) |
| `<option><name>CCIncludePath2</name>...</option>` | Append one new `<state>$PROJ_DIR$/../app/<sub></state>` child element per subdirectory. Each path lives in its own `<state>` sibling (IAR 9.x convention), matching the 21 SDK-bundled paths that already use this shape. |

Both edits are **idempotent** - re-running the script after a manual
IAR IDE edit that already added the files is a no-op.

### How to verify before commit

```bash
python tools/ewp_add_files.py            # dry-run, prints diff
python tools/ewp_add_files.py --check    # exit 1 if anything missing
```

The `--check` form is suitable for CI on every PR.

## What was actually missing

| Path | Type | Why it was missing |
|---|---|---|
| `app/can/can_db.{c,h}`            | DBC facade          | added in 2bd1d44 |
| `app/can/can_db_codec.{c,h}`      | bit-ex/encode       | added in 2bd1d44 |
| `app/can/can_db_ipk_gen.{c,h}`    | IPK msg table       | added in 2bd1d44 |
| `app/can/can_if.{c,h}`            | FLEXCAN ring        | added in ad1997f |
| `app/can/can_rx.{c,h}`            | 5ms drain + dispatch | added in ad1997f |
| `app/can/can_tx.{c,h}`            | encode + trigger    | added in ad1997f |
| `app/init/bsp_init.{c,h}`         | board bring-up      | added in baeb6cc |
| `app/init/drv_init.{c,h}`         | driver init         | added in baeb6cc |
| `app/log/log.{c,h}`               | 4-level logger      | added in baeb6cc |
| `app/rti/rti.{c,h}`               | 1ms time source     | added in baeb6cc |
| `app/scheduler/scheduler.{c,h}`   | module registry     | added in baeb6cc |
| `app/signal/signal.{c,h}`         | signal bus          | added in 80ff2e3 |
| `app/signal/signal_test.c`        | unit test           | added in 80ff2e3 |
| `app/storage/kv.{c,h}`            | KV store            | added in 16fd35e |
| `app/mod_template/{c,h}`          | skeleton module     | added in baeb6cc |
| `app/result.h`, `app/types.h`     | shared headers      | added in 80ff2e3 / a9d5315 |
| `EWARM/C02_B2.ewp` (rewrite)      | IAR project         | added in this commit |

Total: **31 source/header entries + 8 include paths** added to the
project file.

## Build host checklist

- [ ] Windows 10/11 x64
- [ ] IAR Embedded Workbench for ARM 9.30 or later (9.40.1 tested)
- [ ] License: `iarsystemsactivate` (or floating license server URL)
- [ ] Git for Windows
- [ ] Python 3.9+ (only for `ewp_add_files.py`)
- [ ] `C:/Program Files/IAR Systems/Embedded Workbench 9.40/common/bin`
      on `PATH` (or set `IAR_BIN` env var)

## Build steps (clean machine)

```bash
git clone git@github.com:CC-GIT-MAX/GY-C02-B2.git
cd GY-C02-B2
git checkout codex/opt-arch

# 1. Make sure IAR can see every source
python tools/ewp_add_files.py --apply

# 2. (a) command-line build
tools/iar_build.sh

# 2. (b) or interactive
#     "C:/Program Files/IAR Systems/Embedded Workbench 9.40/common/bin/IarIdePm.exe" \
#         EWARM/C02_B2.eww
#     Project -> Rebuild All

# 3. Inspect
ls EWARM/FLASH/Exe/C02_B2.out
```

## Reserved / future-callable APIs

A handful of public symbols are intentionally exposed even though no
internal caller exercises them yet. They exist so that forthcoming
business modules (mod_can_rx, mod_can_tx, mod_can_demo, mod_rti_demo + future mod_power/mod_meter/mod_diag) can plug in
without touching the existing module headers. None of this is dead
code; every API is wired and tested at the unit level where it makes
sense.

| Header | Public symbol(s) | Reserved for |
|---|---|---|
| `app/can/can_rx.h`         | `extern const mod_desc_t mod_can_rx;` | `Scheduler_Init()` hook |
| `app/can/can_tx.h`         | `extern const mod_desc_t mod_can_tx;` <br> `CanTx_EncodeSignal` / `CanTx_PreparePayload` / `CanTx_RebuildFromSignals` / `CanTx_SetCycle` / `CanTx_Trigger` | `Scheduler_Init()` hook <br> Business modules that emit CAN frames |
| `app/mod_template/mod_template.h` | `extern const mod_desc_t mod_template;` <br> `Template_SetDiagValue` / `Template_GetDiagValue` | `Scheduler_Init()` hook <br> Debug / unit-test injection |
| `app/storage/kv.h`         | `KV_Init` / `KV_Get` / `KV_Set` / `KV_Delete` / `KV_Commit` / `KV_IsDirty` | future mod_storage, configuration |
| `app/signal/signal_test.c` | `Signal_TestRun` | host CI runner (gcc/clang) and on-target debug menu |

**Linker note**: `signal_test.c` is currently included in the
on-target build because `tools/ewp_add_files.py` treats every
`app/signal/*.c` as build input. It compiles cleanly (the function
body is non-empty, so the linker keeps it) and adds about 4 KB of
flash. To exclude it from on-target without losing it on host, add
a `#ifndef C02B2_HOST_TEST` guard around the body and define the
macro only in the host test runner. Tracked as a follow-up; not
blocking.