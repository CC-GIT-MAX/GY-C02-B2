#!/usr/bin/env python3
"""
@brief   One-shot regenerator for the IPK DBC-derived CAN artefacts.
@brief   一键刷新 IPK DBC 派生的 CAN 工件

@details Drives tools/dbc_parse.py with all side-effect flags and
         splices the freshly generated blocks back into the
         source files that embed them:
           - app/drv_api/can/can_db_ipk_gen.{h,c}  (rewritten by --split)
           - app/signal/signal.h                   (SIG_CAN_<Name> block)
           - app/drv_api/can/can_db.c              (s_dbc_to_bus[] body)
           - app/can/can_tx.c                      (g_can_tx_cycle_table[] body)
           - app/can/can_rx.c                      (g_can_rx_timeout_table[] body)

         Each splice anchors on the existing table / banner line, so
         it tolerates hand-tweaked prose around the table.

Usage:
    python tools/regen_can_artifacts.py --dbc path/to/IPK.dbc \
        [--node IPK] [--rx 64] [--tx 9] [--out app/drv_api/can] [--dry-run]

@return  exit 0 on full pass; non-zero on any failure
"""
from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
PARSER    = REPO_ROOT / "tools" / "dbc_parse.py"
SIGNAL_H  = REPO_ROOT / "app" / "signal" / "signal.h"
CAN_DB_C  = REPO_ROOT / "app" / "drv_api" / "can" / "can_db.c"
CAN_TX_C  = REPO_ROOT / "app" / "can" / "can_tx.c"
CAN_RX_C  = REPO_ROOT / "app" / "can" / "can_rx.c"
GEN_OUT   = REPO_ROOT / "app" / "drv_api" / "can"
SCRATCH   = REPO_ROOT / "tools" / "_dbc_scratch"


def read(p: Path) -> str:
    return p.read_text(encoding="utf-8")


def write(path: Path, content: str, dry: bool, label: str):
    old = read(path)
    if old == content:
        print(f"[ok]  {label}: unchanged")
        return
    a, b = len(old.splitlines()), len(content.splitlines())
    if dry:
        print(f"[dry] {label}: {a} -> {b} lines")
        return
    path.write_text(content, encoding="utf-8")
    print(f"[wrt] {label}: {a} -> {b} lines")


def backup(p: Path):
    bak = p.with_suffix(p.suffix + ".bak")
    if not bak.exists():
        shutil.copy2(p, bak)


def _strip_block_header(raw: str) -> str:
    """Drop lines up to and including the table-declaration line that
    opens with `{`.  What remains is the table body (entries + closing
    `};`  on its own line)."""
    lines = raw.splitlines()
    for i, ln in enumerate(lines):
        if "{" in ln:
            body = lines[i+1:] if "{" in lines[i] else lines[i:]
            # Drop the trailing `};` line so the splice later still ends cleanly.
            while body and body[-1].strip() == "};":
                body = body[:-1]
            return "\n".join(body).rstrip() + "\n"
    sys.exit("block has no opening `{`")


def run_parser(dbc: Path, node: str, rx_n: int, tx_n: int,
               out_dir: Path) -> dict:
    SCRATCH.mkdir(exist_ok=True)
    s = SCRATCH / "signal.txt"
    m = SCRATCH / "map.txt"
    t = SCRATCH / "tx.txt"
    r = SCRATCH / "rx.txt"
    cmd = [sys.executable, str(PARSER), str(dbc), node,
           str(rx_n), str(tx_n),
           "--split", str(out_dir),
           "--emit-signal-block", str(s),
           "--emit-map", str(m),
           "--emit-tables", str(t), str(r)]
    print("[regen] $ " + " ".join(cmd))
    rc = subprocess.run(cmd, check=False).returncode
    if rc != 0:
        sys.exit(f"dbc_parse.py failed ({rc})")
    return dict(signal=s, map=m, cycle=t, timeout=r)


def splice_signal_block(block: Path, target: Path, dry: bool):
    """Replace the SIG_CAN_<Name> block emitted by the DBC parser.

    Anchor on the first un-indented message banner `/* 0xMMM ... */`
    and the line just above SIG_MAX.  Hand-written signals above are
    preserved."""
    src = read(target)
    new = read(block).rstrip() + "\n"
    pat = (r"(?:^|\n)(/\* 0x[0-9A-F]{3,8} [A-Za-z0-9_]+ "
           r"\([RT]X\)  dlc=\d+ \*/\n)")
    head = re.search(pat, src)
    if not head:
        sys.exit("signal.h: no autoblock banner found")
    start = head.start(1)
    max_pat = r"(?:^|\n)(    SIG_MAX\b)"
    end_m = re.search(max_pat, src[start:])
    if not end_m:
        sys.exit("signal.h: cannot locate SIG_MAX")
    end = start + end_m.start(1)
    out = src[:start] + new + src[end:]
    write(target, out, dry, "signal.h CAN autoblock")


def splice_table_body(block: Path, target: Path, head_pat: str,
                      dry: bool, label: str):
    """Generic table-body splice (strips leading AUTOGEN header)."""
    src = read(target)
    body = _strip_block_header(read(block))
    head = re.search(head_pat, src)
    if not head:
        sys.exit(target.name + ": pattern not found")
    s = head.end()
    em = re.search("\n};\n", src[s:])
    if not em:
        sys.exit(target.name + ": closing missing")
    e = s + em.start() + 1
    out = src[:s] + body + src[e:]
    write(target, out, dry, label)


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument("--dbc", required=True, type=Path)
    ap.add_argument("--node", default="IPK")
    ap.add_argument("--rx", type=int, default=64)
    ap.add_argument("--tx", type=int, default=9)
    ap.add_argument("--out", type=Path, default=GEN_OUT)
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()
    if not a.dbc.exists():
        sys.exit("--dbc missing")
    if not PARSER.exists():
        sys.exit("dbc_parse.py missing at " + str(PARSER))
    arts = run_parser(a.dbc, a.node, a.rx, a.tx, a.out)
    if not a.dry_run:
        for p in (SIGNAL_H, CAN_DB_C, CAN_TX_C, CAN_RX_C):
            backup(p)
    splice_signal_block(arts["signal"], SIGNAL_H, a.dry_run)
    splice_table_body(
        arts["map"], CAN_DB_C,
        r"(static const signal_id_t s_dbc_to_bus\[CAN_DB_IPK_SIG_COUNT\] = \{\n)",
        a.dry_run, "can_db.c s_dbc_to_bus",
    )
    splice_table_body(
        arts["cycle"], CAN_TX_C,
        r"(static const u16 g_can_tx_cycle_table\[CAN_DB_IPK_MSG_COUNT\] = \{\n)",
        a.dry_run, "can_tx.c g_can_tx_cycle_table",
    )
    splice_table_body(
        arts["timeout"], CAN_RX_C,
        r"(static const u16 g_can_rx_timeout_table\[CAN_DB_IPK_MSG_COUNT\] = \{\n)",
        a.dry_run, "can_rx.c g_can_rx_timeout_table",
    )
    print("[regen] all 5 artefacts refreshed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
