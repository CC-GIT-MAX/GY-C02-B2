#!/usr/bin/env python3
"""
@brief   Parse a Vector CAN DBC file and emit C-language tables for the
         C02-B2 cluster project.
@brief   解析 Vector CAN DBC 文件, 输出 C02-B2 仪表工程可直接 include 的
         C 语言报文/信号描述表.

@param[in]  argv[1]  path to the input .dbc file
@param[in]  argv[2]  target node name (default: IPK)
@param[in]  argv[3]  max RX messages to emit (default: 64)
@param[in]  argv[4]  max TX messages to emit (default: 9)
@param[out] stdout or --split out_dir writes generated source files

@return  exit 0 on success

Side-effect flags (write artefacts to disk, can be combined):
    --split OUT_DIR        Write can_db_<node>_gen.{h,c} into OUT_DIR
    --emit-signal-block OUT
                           Write app/signal/signal.h CAN section to OUT
                           (splice between SIG_CAN_RX_TIMEOUT_MAP_HI and
                           SIG_MAX; consumer script handles the splice)
    --emit-map OUT         Write the s_dbc_to_bus[CAN_DB_IPK_SIG_COUNT]
                           designated-init body (without the array
                           declaration) to OUT.  Splice in can_db.c.
    --emit-tables OUT_TX OUT_RX
                           Write g_can_tx_cycle_table and
                           g_can_rx_timeout_table bodies (without the
                           static const u16 ... declarations) to OUT_TX
                           and OUT_RX.  Splice in can_tx.c and can_rx.c.

Usage:
    python tools/dbc_parse.py <dbc> [node] [rx_n] [tx_n]
    python tools/dbc_parse.py <dbc> IPK 64 9 --split app/drv_api/can
    python tools/dbc_parse.py <dbc> IPK 64 9 \
        --emit-signal-block _signal_can_block.txt \
        --emit-map _s_dbc_to_bus_block.txt \
        --emit-tables _cycle.txt _timeout.txt
"""
from __future__ import annotations

import re
import sys
from dataclasses import dataclass, field
from typing import List, Optional, Tuple
from pathlib import Path


@dataclass
class Signal:
    name: str
    start_bit: int
    length: int
    byte_order: int
    is_signed: bool
    factor: float
    offset: float
    minimum: float
    maximum: float
    unit: str
    receivers: List[str] = field(default_factory=list)


@dataclass
class Message:
    can_id: int
    name: str
    dlc: int
    transmitter: str
    signals: List[Signal] = field(default_factory=list)


_RE_BO = re.compile(r"^\s*BO_\s+(\d+)\s+(\S+)\s*:\s*(\d+)\s+(\S+)")
# Two-stage SG_ parser: first grab the signal name as the first \S+ token,
# then match the rest. Using a single regex with (\S+)\s*(?:\w+\s*:\s*)? causes
# the re engine to backtrack and truncate the signal name when the optional
# multiplexer group fails to match (e.g. "MMI_Second" -> "MMI_Secon").
_RE_SG_HEAD = re.compile(r"^\s*SG_\s+(\S+)(.*)$")
_RE_SG_REST = re.compile(
    r"^(?:\s+\w+)?\s*:?\s*(\d+)\s*\|\s*(\d+)@\s*([01])([+-])"
    r"\s*\(\s*([-+0-9.eE]+)\s*,\s*([-+0-9.eE]+)\s*\)"
    r"\s*\[\s*([-+0-9.eE]+)\s*\|\s*([-+0-9.eE]+)\s*\]"
    r"\s*\"([^\"]*)\"\s*(.*)$"
)


def parse_dbc(path: str) -> List[Message]:
    msgs: List[Message] = []
    cur: Optional[Message] = None
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            m = _RE_BO.match(line)
            if m:
                can_id = int(m.group(1))
                if can_id >= 0x80000000:
                    cur = None
                    continue
                cur = Message(can_id=can_id, name=m.group(2),
                              dlc=int(m.group(3)), transmitter=m.group(4))
                msgs.append(cur)
                continue
            if cur is None:
                continue
            head = _RE_SG_HEAD.match(line)
            if not head:
                continue
            name = head.group(1)
            rest_str = head.group(2)
            m = _RE_SG_REST.match(rest_str)
            if not m:
                continue
            receivers_raw = m.group(10).strip()
            receivers = [r.strip() for r in receivers_raw.split(",") if r.strip()]
            cur.signals.append(Signal(
                name=name,
                start_bit=int(m.group(1)),
                length=int(m.group(2)),
                byte_order=int(m.group(3)),
                is_signed=(m.group(4) == "-"),
                factor=float(m.group(5)),
                offset=float(m.group(6)),
                minimum=float(m.group(7)),
                maximum=float(m.group(8)),
                unit=m.group(9),
                receivers=receivers,
            ))
    return msgs


def select_for_node(msgs, node, rx_n, tx_n):
    rx = []
    tx = []
    for m in msgs:
        if m.transmitter == node and len(tx) < tx_n:
            tx.append(m)
            continue
        if any(node in s.receivers for s in m.signals):
            if len(rx) < rx_n:
                rx.append(m)
    return rx, tx


def _raw_type_for_length(sig):
    if sig.length <= 8:
        return "I8" if sig.is_signed else "U8"
    if sig.length <= 16:
        return "I16" if sig.is_signed else "U16"
    if sig.length <= 32:
        return "I32" if sig.is_signed else "U32"
    return "I64" if sig.is_signed else "U64"


def _format_float(v):
    if v == int(v):
        return f"{int(v)}.0"
    return f"{v:.8g}"


def emit_header(node, msgs, enum_names):
    guard = f"CAN_DB_{node.upper()}_GEN_H"
    L = []
    L.append("/* AUTOGENERATED by tools/dbc_parse.py -- do not edit by hand. */")
    L.append(f"#ifndef {guard}")
    L.append(f"#define {guard}")
    L.append("")
    L.append("#include <stdint.h>")
    L.append("#include \"can_db_codec.h\"")
    L.append("")
    L.append(f"/* Signal IDs -- order matches can_sig_descs_{node.lower()}[] */")
    L.append("typedef enum {")
    L.append("    CAN_DB_SIG_INVALID = 0, /* [  0] invalid sentinel */")
    # Emit one-based index in the trailing comment for fast lookup
    # against can_sig_descs_<node>[]. The index also matches the
    # "i + 1" mapping used by prv_find_sig_in_msg() in can_tx.c.
    for i, n in enumerate(enum_names, start=1):
        L.append(f"    {n}, /* [{i:>3}] */")
    L.append("    CAN_DB_SIG_MAX /* [total] end-of-enum sentinel */")
    L.append(f"}} can_db_sig_id_t_{node.lower()};")
    L.append("")
    rx_n = sum(1 for m in msgs if m.transmitter != node)
    tx_n = sum(1 for m in msgs if m.transmitter == node)
    L.append(f"#define CAN_DB_{node.upper()}_MSG_COUNT  ({len(msgs)}u)")
    L.append(f"#define CAN_DB_{node.upper()}_SIG_COUNT  ({len(enum_names)}u)")
    L.append(f"#define CAN_DB_{node.upper()}_RX_COUNT   ({rx_n}u)")
    L.append(f"#define CAN_DB_{node.upper()}_TX_COUNT   ({tx_n}u)")
    L.append("")
    L.append(f"#endif /* {guard} */")
    L.append("")
    return "\n".join(L)


def emit_source(node, msgs, enum_names):
    L = []
    L.append("/* AUTOGENERATED by tools/dbc_parse.py -- do not edit by hand. */")
    L.append(f"#include \"can_db_{node.lower()}_gen.h\"")
    L.append("")
    L.append("/* === Per-signal descriptors === */")
    L.append(f"const can_sig_desc_t can_sig_descs_{node.lower()}[] = {{")
    # Emit, grouped by message, with a direction banner per msg.
    # Every signal is also tagged (RX) or (TX) on its opening comment
    # so a grep tells a consumer at a glance which direction is which.
    for m in msgs:
        is_tx_msg = (m.transmitter == node)
        dir_tag = "TX" if is_tx_msg else "RX"
        L.append(f"    /* --- {dir_tag}: {m.name} (0x{m.can_id:04X}, dlc={m.dlc}) --- */")
        for s in m.signals:
            raw_t = "CAN_RAW_" + _raw_type_for_length(s)
            L.append(
                f"    {{ /* {s.name} in {m.name} ({dir_tag}) */\n"
                f"        .start_bit    = {s.start_bit},\n"
                f"        .length       = {s.length},\n"
                f"        .byte_order   = {s.byte_order},\n"
                f"        .is_signed    = {1 if s.is_signed else 0},\n"
                f"        .factor       = {_format_float_c(s.factor)}f,\n"
                f"        .offset       = {_format_float_c(s.offset)}f,\n"
                f"        .raw_type     = {raw_t},\n"
                f"    }},\n"
            )
    L.append("};")
    L.append("")
    L.append("/* === Per-message descriptors === */")
    L.append(f"const can_msg_desc_t can_msg_descs_{node.lower()}[] = {{")
    sig_index = 0
    for m in msgs:
        n = len(m.signals)
        is_tx_msg = (m.transmitter == node)
        dir_tag = "TX" if is_tx_msg else "RX"
        L.append(
            f"    {{ /* {m.name} ({dir_tag}) */\n"
            f"        .can_id     = 0x{m.can_id:04X}u,\n"
            f"        .dlc        = {m.dlc}u,\n"
            f"        .name       = \"{m.name}\",\n"
            f"        .sig_index  = {sig_index}u,\n"
            f"        .sig_count  = {n}u,\n"
            f"        .tx_node    = \"{m.transmitter}\",\n"
            f"        .is_tx      = {(1 if m.transmitter == node else 0)},\n"
            f"    }},\n"
        )
        sig_index += n
    L.append("};")
    L.append("")
    L.append("/* === Index tables (RX / TX) === */")
    rx_idx = [i for i, m in enumerate(msgs) if m.transmitter != node]
    tx_idx = [i for i, m in enumerate(msgs) if m.transmitter == node]
    L.append(f"const uint16_t can_db_{node.lower()}_rx_idx[] = {{")
    L.append("    " + (", ".join(f"{i}u" for i in rx_idx) + ("," if rx_idx else "")))
    L.append("};")
    L.append(f"const uint16_t can_db_{node.lower()}_tx_idx[] = {{")
    L.append("    " + (", ".join(f"{i}u" for i in tx_idx) + ("," if tx_idx else "")))
    L.append("};")
    L.append("")
    L.append(f"#define CAN_DB_{node.upper()}_RX_COUNT  ({len(rx_idx)}u)")
    L.append(f"#define CAN_DB_{node.upper()}_TX_COUNT  ({len(tx_idx)}u)")
    L.append("")
    return "\n".join(L)



def _format_float(v):
    """Format a float for human display (signal annotations)."""
    if v == int(v):
        return f"{int(v)}"
    s = f"{v:.6g}"
    return s if s else "0"

def _format_float_c(v):
    """Format a float as a C99 float literal, always with a decimal
    point and at most 9 significant digits. Output without a trailing
    f; caller appends that suffix."""
    iv = int(v)
    if v == iv:
        return f"{iv}.0"
    s = f"{v:.9g}"
    return s if s else "0.0"


def emit_signal_block(selected):
    """Emit the SIG_CAN_<Name> block for app/signal/signal.h.

    Each entry is annotated with raw-range and factor/offset so a reader
    can tell what kind of value to expect on the signal bus.  Grouped by
    message with a /* 0x<id> <name> (RX|TX)  dlc=<n> */ banner.
    """
    out = []
    sig_iter = []
    for m in selected:
        for s in m.signals:
            sig_iter.append((m, s))
    # signals inside a message
    sig_idx = 0
    for m in selected:
        rx_tx = "TX" if m.transmitter == _get_self_node() else "RX"
        out.append(f"/* 0x{m.can_id:03X} {m.name} ({rx_tx})  dlc={m.dlc} */")
        for s in m.signals:
            length = s.length
            is_signed = bool(s.is_signed)
            if is_signed:
                vmin = -(1 << (length - 1)) * s.factor + s.offset
                vmax = ((1 << (length - 1)) - 1) * s.factor + s.offset
            else:
                vmin = 0.0 * s.factor + s.offset
                vmax = ((1 << length) - 1) * s.factor + s.offset
            def _f(v):
                if v == int(v):
                    return f"{int(v)}"
                s_ = f"{v:.6g}"
                return s_ if s_ else "0"
            fact_str = f"raw*{_f(s.factor)}" if s.factor != 1.0 else "raw*1"
            if s.offset != 0.0:
                op = "+" if s.offset >= 0 else ""
                fact_str += f"{op}{_f(s.offset)}"
            rng = f"[{_f(vmin)}..{_f(vmax)}]"
            out.append(
                f"    SIG_CAN_{s.name},/* {length}bit "
                f"{'signed' if is_signed else 'unsigned'} {fact_str} {rng} */"
            )
            sig_idx += 1
    return "\n".join(out) + "\n"


def emit_map_body(selected):
    """Emit the s_dbc_to_bus[] designated-init body for can_db.c.

    Entries are grouped by source message.  Each group starts
    with a `/* --- RX|TX: <name> (0x<id>, dlc=<n>) --- */` banner
    so a reader can find signals by message without scrolling.
    The runtime layout (designated-init `[k - 1u] = ...`) is unchanged.
    """
    out = ["static const signal_id_t s_dbc_to_bus[CAN_DB_IPK_SIG_COUNT] = {",
           "    [0] = SIG_INVALID, /* CAN_DB_SIG_INVALID */"]
    node = _get_self_node()
    for m in selected:
        dir_tag = "TX" if m.transmitter == node else "RX"
        out.append(f"    /* --- {dir_tag}: {m.name} (0x{m.can_id:04X}, dlc={m.dlc}) --- */")
        for s in m.signals:
            out.append(f"    [CAN_DB_SIG_{s.name} - 1u] = SIG_CAN_{s.name},")
    out.append("};")
    return "\n".join(out) + "\n"


def _default_cycle_ms(msg):
    """Heuristic default cyclic send period for a TX message (ms).

    100 ms for STS / SettingRequest, 500 ms for fuel / odo / datetime,
    1 s for service / NWM / everything else.
    """
    n = msg.name.lower()
    if "sts" in n or "settingrequest" in n:
        return 100
    if any(k in n for k in ("fuel_info", "fuel_sts", "odo", "datetime", "nwm")):
        return 500
    return 1000


def emit_tx_cycle_table(selected, node):
    """Emit g_can_tx_cycle_table[CAN_DB_IPK_MSG_COUNT] body."""
    out = [
        "/* AUTOGENERATED by tools/dbc_parse.py. */",
        "static const u16 g_can_tx_cycle_table[CAN_DB_IPK_MSG_COUNT] = {",
    ]
    for i, m in enumerate(selected):
        rx_tx = "TX" if m.transmitter == node else "RX"
        cyc = _default_cycle_ms(m) if m.transmitter == node else 0
        out.append(f"    /* {i:>3} {m.name:<32} ({rx_tx}) */ {cyc}u,")
    out.append("};")
    return "\n".join(out) + "\n"


def emit_rx_timeout_table(selected, node):
    """Emit g_can_rx_timeout_table[CAN_DB_IPK_MSG_COUNT] body.

    Heuristic: cycle * 3 rounded up to nearest 50 ms.  TX rows = 0
    (not monitored).  Unknown cycle = 0 (no monitoring).
    """
    out = [
        "/* AUTOGENERATED by tools/dbc_parse.py. */",
        "static const u16 g_can_rx_timeout_table[CAN_DB_IPK_MSG_COUNT] = {",
    ]
    for i, m in enumerate(selected):
        rx_tx = "TX" if m.transmitter == node else "RX"
        if m.transmitter == node:
            tmo = 0  # TX rows not monitored
            note = "  /* TX row, not monitored */"
        else:
            cyc = _default_cycle_ms(m)
            if cyc == 0:
                tmo = 0
                note = ""
            else:
                tmo = ((cyc * 3 + 49) // 50) * 50
                note = "  /* cycle * 3, round 50 ms */"
        out.append(f"    /* {i:>3} {m.name:<32} ({rx_tx}) */ {tmo}u,{note}")
    out.append("};")
    return "\n".join(out) + "\n"


def report_diffs(old_dbc: str, new_dbc: str, node: str) -> int:
    """Compute the diff between two DBCs (message / signal add-remove-modify).

    Returns 0 on success; non-zero on read failure.  Writes a human-readable
    diff to stdout and an exit-status line:

        rc=0  no SOC-touching changes (purely DBC internal)
        rc=1  message-level add/remove   (potential SOC impact)
        rc=2  signal-level add/remove   (potential SOC impact)
        rc=3  signal-level modify only  (DBC only, no SOC impact if SOC
               reads the runtime Signal_Get(), not the raw DBC layout)

    The rc is also printed so callers / CI can grep for it.
    """
    try:
        old_msgs = parse_dbc(old_dbc)
        new_msgs = parse_dbc(new_dbc)
    except Exception as ex:
        print(f"diff: parse error: {ex}", file=sys.stderr)
        return 4

    def key(msg): return msg.can_id
    old = {key(m): m for m in old_msgs}
    new = {key(m): m for m in new_msgs}
    added_ids   = sorted(set(new) - set(old))
    removed_ids = sorted(set(old) - set(new))
    common_ids  = sorted(set(old) & set(new))

    sig_added = sig_removed = sig_modified = 0
    msg_added = len(added_ids)
    msg_removed = len(removed_ids)

    print("=== DBC diff ============================================")
    print(f"old : {old_dbc}")
    print(f"new : {new_dbc}")
    print(f"node: {node}")
    print()

    if msg_added:
        print(f"[+] messages added   ({msg_added}):")
        for mid in added_ids:
            m = new[mid]
            tx_dir = "TX" if m.transmitter == node else "RX"
            print(f"    + 0x{mid:04X} {m.name} ({tx_dir}, dlc={m.dlc}, {len(m.signals)} sigs)")
    if msg_removed:
        print(f"[-] messages removed ({msg_removed}):")
        for mid in removed_ids:
            m = old[mid]
            tx_dir = "TX" if m.transmitter == node else "RX"
            print(f"    - 0x{mid:04X} {m.name} ({tx_dir}, dlc={m.dlc}, {len(m.signals)} sigs)")
    if msg_added or msg_removed:
        print()

    for mid in common_ids:
        o = old[mid]; n = new[mid]
        o_sigs = {s.name: s for s in o.signals}
        n_sigs = {s.name: s for s in n.signals}
        msg_sig_added   = sorted(set(n_sigs) - set(o_sigs))
        msg_sig_removed = sorted(set(o_sigs) - set(n_sigs))
        msg_sig_changed = []
        for nm in set(o_sigs) & set(n_sigs):
            os, ns = o_sigs[nm], n_sigs[nm]
            if (os.start_bit != ns.start_bit or os.length != ns.length
                or os.byte_order != ns.byte_order or os.is_signed != ns.is_signed
                or abs(os.factor - ns.factor) > 1e-9 or abs(os.offset - ns.offset) > 1e-9):
                msg_sig_changed.append(nm)
        if msg_sig_added or msg_sig_removed or msg_sig_changed:
            print(f"[*] 0x{mid:04X} {n.name}:")
            for nm in msg_sig_added:
                s = n_sigs[nm]
                print(f"        + signal {nm} (start={s.start_bit}, len={s.length}bit, factor={s.factor})")
                sig_added += 1
            for nm in msg_sig_removed:
                s = o_sigs[nm]
                print(f"        - signal {nm} (was start={s.start_bit}, len={s.length}bit)")
                sig_removed += 1
            for nm in msg_sig_changed:
                os = o_sigs[nm]; ns = n_sigs[nm]
                diff = []
                if os.start_bit != ns.start_bit:   diff.append(f"start_bit {os.start_bit} -> {ns.start_bit}")
                if os.length != ns.length:         diff.append(f"length {os.length} -> {ns.length}")
                if os.byte_order != ns.byte_order: diff.append(f"byte_order {os.byte_order} -> {ns.byte_order}")
                if os.is_signed != ns.is_signed:   diff.append(f"is_signed {os.is_signed} -> {ns.is_signed}")
                if abs(os.factor - ns.factor) > 1e-9: diff.append(f"factor {os.factor} -> {ns.factor}")
                if abs(os.offset - ns.offset) > 1e-9: diff.append(f"offset {os.offset} -> {ns.offset}")
                print(f"        ~ signal {nm}: " + ", ".join(diff))
                sig_modified += 1

    print()
    print("=== Summary ===============================================")
    print(f"  messages : +{msg_added} -{msg_removed}")
    print(f"  signals  : +{sig_added} -{sig_removed} (modified {sig_modified})")
    print()
    if msg_added or msg_removed:
        rc = 1
    elif sig_added or sig_removed:
        rc = 2
    elif sig_modified:
        rc = 3
    else:
        rc = 0
    print(f"SOC protocol impact: " + (
        "MESSAGES ADDED/REMOVED (treat as SOC protocol revision!)" if rc == 1 else
        "SIGNALS ADDED/REMOVED (SOC team must review)" if rc == 2 else
        "ONLY SIGNAL LAYOUT CHANGED (DBC internal)" if rc == 3 else
        "NO change"))
    print(f"diff-rc={rc}")
    return rc


def _get_self_node():
    """Helper: the node argument passed by the user (set by main())."""
    return _SELF_NODE[0]


_SELF_NODE = [None]


def main():
    if len(sys.argv) < 2 or sys.argv[1] in ("-h", "--help"):
        print(__doc__, file=sys.stderr)
        return 2
    # --report-diffs OLD NEW [--node X] is its own command and takes
    # TWO positional path args; handle it before the default argv shape
    # below tries to int() them.
    if sys.argv[1] == "--report-diffs":
        # form: --report-diffs OLD NEW [--node X]
        args = sys.argv[2:]
        if len(args) < 2:
            print("usage: --report-diffs OLD_DBC NEW_DBC [--node X]", file=sys.stderr)
            return 2
        old_dbc, new_dbc = args[0], args[1]
        node = "IPK"
        if "--node" in args:
            j = args.index("--node")
            node = args[j + 1] if j + 1 < len(args) else "IPK"
        return report_diffs(old_dbc, new_dbc, node)
    dbc_path = sys.argv[1]
    node = sys.argv[2] if len(sys.argv) > 2 else "IPK"
    # IPK node defaults to 64 RX + 9 TX per C02-B2 cluster DBC.
    rx_n = int(sys.argv[3]) if len(sys.argv) > 3 else 64
    tx_n = int(sys.argv[4]) if len(sys.argv) > 4 else 9

    all_msgs = parse_dbc(dbc_path)
    rx, tx = select_for_node(all_msgs, node, rx_n, tx_n)
    selected = rx + tx

    enum_names = []
    for m in selected:
        for s in m.signals:
            enum_names.append("CAN_DB_SIG_" + s.name)

    header = emit_header(node, selected, enum_names)
    source = emit_source(node, selected, enum_names)

    _SELF_NODE[0] = node

    if "--emit-signal-block" in sys.argv:
        out = sys.argv[sys.argv.index("--emit-signal-block") + 1]
        Path(out).write_text(emit_signal_block(selected), encoding="utf-8")
        print(f"signal block -> {out}", file=sys.stderr)

    if "--emit-map" in sys.argv:
        out = sys.argv[sys.argv.index("--emit-map") + 1]
        Path(out).write_text(emit_map_body(selected), encoding="utf-8")
        print(f"map body -> {out}", file=sys.stderr)

    if "--emit-tables" in sys.argv:
        i = sys.argv.index("--emit-tables")
        out_tx = sys.argv[i + 1]
        out_rx = sys.argv[i + 2]
        Path(out_tx).write_text(emit_tx_cycle_table(selected, node), encoding="utf-8")
        Path(out_rx).write_text(emit_rx_timeout_table(selected, node), encoding="utf-8")
        print(f"cycle table -> {out_tx}", file=sys.stderr)
        print(f"timeout table -> {out_rx}", file=sys.stderr)

    if "--split" in sys.argv:
        out_dir = sys.argv[sys.argv.index("--split") + 1]
        with open(f"{out_dir}/can_db_{node.lower()}_gen.h", "w", encoding="utf-8") as f:
            f.write(header)
        with open(f"{out_dir}/can_db_{node.lower()}_gen.c", "w", encoding="utf-8") as f:
            f.write(source)
    elif not any(f in sys.argv for f in ("--emit-signal-block", "--emit-map", "--emit-tables")):
        sys.stdout.write(f"/* === FILE: can_db_{node.lower()}_gen.h === */\n")
        sys.stdout.write(header)
        sys.stdout.write(f"/* === FILE: can_db_{node.lower()}_gen.c === */\n")
        sys.stdout.write(source)
    return 0


if __name__ == "__main__":
    sys.exit(main())
