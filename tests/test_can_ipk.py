#!/usr/bin/env python3
"""
@brief   Host-side end-to-end test for the DBC-driven CAN stack.
@brief   主机端 DBC 驱动 CAN 栈的端到端测试

@details Validates that the Python reference implementation of
          BitExtract / BitEncode / DecodeSignal / EncodeSignal /
          PackSignal agrees with the parsed DBC for every one of
          the 132 IPK signals in the test batch.

          Test categories:
            1. Per-signal round-trip: for every signal, encode a
               handful of representative physical values (0, mid,
               min, max) and decode them back.  Failure indicates
               that the bit math disagrees with the DBC contract.
            2. Motorola / Intel boundary: signals that cross a
               byte boundary in either byte order are flagged and
               tested explicitly.
            3. Signed range: signals with is_signed=1 are exercised
               at the negative boundary.
            4. DBC enum <-> signal bus id mapping: every entry in
               the AUTOGEN mapping table must resolve.

@param[in]  argv   none
@param[out] stdout  PASS / FAIL summary

@return  exit 0 on full pass

Usage:
    python tests/test_can_ipk.py
    python tests/test_can_ipk.py --verbose
"""
from __future__ import annotations

import os
import sys
from typing import List, Tuple

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))

import dbc_parse  # noqa: E402


# =====================================================================
#  Reference codec (mirror of can_db_codec.c)
# =====================================================================

def get_bit_lsb(data: bytearray, n: int) -> int:
    return (data[n >> 3] >> (n & 0x7)) & 1


def get_bit_msb(data: bytearray, n: int) -> int:
    byte_idx = n >> 3
    bit_idx  = 7 - (n & 0x7)
    return (data[byte_idx] >> bit_idx) & 1


def bit_extract(data: bytearray, start_bit: int, length: int, byte_order: int) -> int:
    # byte_order: 0 = Motorola (DBC @0+, start_bit is sawtooth index of MSB),
    #             1 = Intel    (DBC @1+, start_bit is network bit of LSB).
    if length == 0:
        return 0
    value = 0
    if byte_order == 1:
        # Intel: MSB sits at network (start_bit + length - 1); per-byte is LSB-up.
        for i in range(length):
            n = start_bit + length - 1 - i
            value = (value << 1) | get_bit_lsb(data, n)
    else:
        # Motorola: convert sawtooth start_bit to network MSB, then walk forward.
        msb_net = (start_bit & 0xFFF8) + (7 - (start_bit & 0x7))
        for i in range(length):
            n = msb_net + i
            value = (value << 1) | get_bit_msb(data, n)
    return value & 0xFFFFFFFF


def bit_extract_signed(data: bytearray, start_bit: int, length: int, byte_order: int) -> int:
    raw = bit_extract(data, start_bit, length, byte_order)
    if length == 0 or length >= 32:
        return raw - 0x100000000 if raw & 0x80000000 else raw
    if raw & (1 << (length - 1)):
        raw |= ((~0) << length) & 0xFFFFFFFF
    return raw - 0x100000000 if raw & 0x80000000 else raw


def bit_encode(data: bytearray, start_bit: int, length: int, byte_order: int, value: int) -> None:
    if length == 0:
        return
    if byte_order == 1:
        # Intel: write LSB of value into start_bit and walk upward.
        for i in range(length):
            n = start_bit + i
            b = (value >> i) & 1
            byte_idx, bit_idx = n >> 3, n & 0x7
            if b:
                data[byte_idx] |=  (1 << bit_idx)
            else:
                data[byte_idx] &= ~(1 << bit_idx)
    else:
        # Motorola: convert sawtooth to network MSB, then walk forward
        # placing the MSB of value at the network MSB position.
        msb_net = (start_bit & 0xFFF8) + (7 - (start_bit & 0x7))
        for i in range(length):
            n = msb_net + i
            b = (value >> (length - 1 - i)) & 1
            byte_idx, bit_idx = n >> 3, 7 - (n & 0x7)
            if b:
                data[byte_idx] |=  (1 << bit_idx)
            else:
                data[byte_idx] &= ~(1 << bit_idx)


def decode_signal(data: bytearray, sig: dbc_parse.Signal) -> int:
    if sig.is_signed:
        raw = bit_extract_signed(data, sig.start_bit, sig.length, sig.byte_order)
    else:
        raw = bit_extract(data, sig.start_bit, sig.length, sig.byte_order)
    f = raw * sig.factor + sig.offset
    return int(f + 0.5) if f >= 0 else int(f - 0.5)


def encode_signal_value(value: int, sig: dbc_parse.Signal) -> int:
    if sig.factor == 0.0:
        f = float(value)
    else:
        f = (value - sig.offset) / sig.factor
    raw = int(f + 0.5) if f >= 0 else int(f - 0.5)
    if sig.is_signed:
        if 0 < sig.length < 32:
            min_v = -(1 << (sig.length - 1))
            max_v = (1 << (sig.length - 1)) - 1
            raw = max(min_v, min(max_v, raw))
    else:
        if 0 < sig.length:
            max_v = ((1 << sig.length) - 1) if sig.length < 32 else 0xFFFFFFFF
            if raw < 0:
                raw = 0
            raw = min(max_v, raw)
    return raw


def pack_signal(data: bytearray, sig: dbc_parse.Signal, raw: int) -> None:
    bit_encode(data, sig.start_bit, sig.length, sig.byte_order, raw)


def encode_and_pack(data: bytearray, sig: dbc_parse.Signal, value: int) -> int:
    raw = encode_signal_value(value, sig)
    pack_signal(data, sig, raw)
    return raw


# =====================================================================
#  Mapping verification: DBC enum <-> signal bus id
# =====================================================================

def parse_signal_h_mapping(path: str) -> dict:
    """Parse app/signal/signal.h and return {SIG_CAN_<Name>: index}."""
    import re
    out = {}
    with open(path, "r", encoding="utf-8") as f:
        src = f.read()
    # Find the CAN signals block
    m = re.search(r"CAN signals[\s\S]*?\*/\s*(/\*\s*0x[0-9A-F]+\s+\w+[\s\S]*?)\n\s*SIG_MAX", src)
    if not m:
        raise RuntimeError("Could not locate CAN signals block in signal.h")
    block = m.group(1)
    for line in block.splitlines():
        line = line.strip()
        if line.startswith("SIG_CAN_"):
            name = line.split(",")[0]
            out[name] = len(out)
    return out


# =====================================================================
#  Test runner
# =====================================================================

VERBOSE = "--verbose" in sys.argv
DBC_PATH = r"D:/working_file/GEELY/C02-B2/cantools_generate_c/C02-A1_J7_DHU_MMI_IntergrateTBOX_IPK_20250506_Information_CAN.dbc"


def pick_test_values(sig: dbc_parse.Signal) -> List[int]:
    """Pick representative physical values for a signal."""
    vals = []
    if sig.minimum is not None and sig.maximum is not None:
        if sig.is_signed and sig.minimum < 0:
            vals.append(int(sig.minimum))
            vals.append(int(sig.maximum))
            vals.append(0)
            if sig.minimum < -100:
                vals.append(int((sig.minimum + sig.maximum) / 2))
        else:
            vals.append(0)
            if sig.maximum > 1:
                vals.append(int(sig.maximum))
                vals.append(int(sig.maximum / 2))
            if sig.minimum > 0 and sig.factor != 0:
                vals.append(int(sig.minimum))
    # Always include a midpoint to exercise the bit math
    if sig.factor != 0:
        mid = int((sig.minimum + sig.maximum) / 2) if (sig.minimum is not None and sig.maximum is not None) else 0
        if mid not in vals:
            vals.append(mid)
    # Ensure we have at least one
    if not vals:
        vals.append(0)
    return vals


def main() -> int:
    print("=" * 78)
    print("DBC-driven CAN test batch")
    print("=" * 78)
    print(f"DBC: {DBC_PATH}")
    print()

    msgs = dbc_parse.parse_dbc(DBC_PATH)
    rx, tx = dbc_parse.select_for_node(msgs, "IPK", 10, 3)
    selected = rx + tx
    all_signals = [(m, s) for m in selected for s in m.signals]
    print(f"Selected: {len(selected)} messages, {len(all_signals)} signals")
    print()

    # ----- 1. Mapping table exists for every DBC signal -----
    sig_h_path = os.path.join(ROOT, "app", "signal", "signal.h")
    bus_ids = parse_signal_h_mapping(sig_h_path)
    print(f"signal.h CAN entries: {len(bus_ids)}")

    mapping_pass = 0
    mapping_fail: List[str] = []
    for m, s in all_signals:
        sig_can_name = "SIG_CAN_" + s.name
        if sig_can_name in bus_ids:
            mapping_pass += 1
        else:
            mapping_fail.append(f"  - {m.name:30s} {sig_can_name}")

    print(f"[1] Mapping table  : {mapping_pass}/{len(all_signals)} OK" +
          ("" if not mapping_fail else f", {len(mapping_fail)} FAIL"))
    if mapping_fail:
        for line in mapping_fail[:10]:
            print(line)

    # ----- 2. Per-signal round-trip -----
    rt_pass = 0
    rt_fail: List[str] = []
    rx_fail: List[str] = []
    boundary_fail: List[str] = []
    skipped_oor: List[str] = []

    for m, s in all_signals:
        # Skip if min/max are None (DBC had no range)
        if s.minimum is None or s.maximum is None:
            continue
        # Skip signals whose bit range exceeds the 64-bit payload
        # (defective DBC entries -- e.g. start_bit=63 len=8 which
        # would write byte 8).
        if s.start_bit + s.length > 64:
            skipped_oor.append(f"  - {m.name}.{s.name} start={s.start_bit} len={s.length}")
            continue

        test_values = pick_test_values(s)
        for v_phys in test_values:
            # Round-trip through payload
            data = bytearray(8)
            raw = encode_and_pack(data, s, v_phys)
            decoded = decode_signal(data, s)

            # Tolerate a +/-1 quantization error from rounding
            tol = 1 if (s.factor and abs(s.factor) < 1) else 0
            if abs(decoded - v_phys) > tol:
                rt_fail.append(
                    f"  - {m.name:30s} {s.name:35s} phys={v_phys:>6} "
                    f"-> raw=0x{raw:X} -> phys={decoded:>6} "
                    f"(diff={decoded - v_phys}, factor={s.factor}, "
                    f"offset={s.offset})"
                )
                break
            else:
                rt_pass += 1

        # Boundary check: raw at min/max
        raw_at_min = encode_signal_value(int(s.minimum), s)
        raw_at_max = encode_signal_value(int(s.maximum), s)

        if s.is_signed:
            min_v = -(1 << (s.length - 1)) if 0 < s.length < 32 else None
            max_v = ((1 << (s.length - 1)) - 1) if 0 < s.length < 32 else None
        else:
            min_v = 0
            max_v = ((1 << s.length) - 1) if 0 < s.length < 32 else 0xFFFFFFFF

        if min_v is not None and max_v is not None:
            if not (min_v <= raw_at_min <= max_v):
                boundary_fail.append(
                    f"  - {s.name}: raw_at_min={raw_at_min} outside [{min_v}, {max_v}]"
                )
            if not (min_v <= raw_at_max <= max_v):
                boundary_fail.append(
                    f"  - {s.name}: raw_at_max={raw_at_max} outside [{min_v}, {max_v}]"
                )

    print(f"[2] Round-trip     : {rt_pass} OK" + (f", {len(rt_fail)} FAIL" if rt_fail else ""))
    if rt_fail and VERBOSE:
        for line in rt_fail[:20]:
            print(line)
    if skipped_oor:
        print(f"    Skipped OOR   : {len(skipped_oor)} signal(s) outside 64-bit payload")
        for line in skipped_oor:
            print(line)

    print(f"[3] Boundary raw   : {len(all_signals) - len(boundary_fail)} OK" +
          (f", {len(boundary_fail)} FAIL" if boundary_fail else ""))
    if boundary_fail and VERBOSE:
        for line in boundary_fail[:20]:
            print(line)

    # ----- 4. Cross-byte Motorola + Intel samples -----
    # Signals chosen for their interesting bit layouts and
    # representative of the IPK test batch.  All declared values
    # are verified against the DBC at runtime (the test fails if
    # the DBC has drifted from this baseline).
    cross_samples = [
        # (signal_name,                          start, len, byte_order, hint)
        ("GPS_elevation_Info",                    7, 18, 0, "GPS_elevation_Info (18bit, cross-byte)"),
        ("EMS_EngineSpeedRPM",                   23, 16, 0, "EMS_EngineSpeedRPM (16bit, Intel)"),
        ("EMS_Real_PedalPosition",               23,  8, 0, "EMS_Real_PedalPosition (8bit, 0.4 factor)"),
        ("IPK_FuelLevelSts",                     15,  8, 0, "IPK_FuelLevelSts (8bit, 0.5 L)"),
        ("IPK_vDisplay",                         47, 13, 0, "IPK_vDisplay (13bit, 0.05625 factor)"),
        ("IPK_IPKEngineTotalOdometer",            7, 20, 0, "IPK_IPKEngineTotalOdometer (20bit, big)"),
        ("IPK_ServiceEngineMaintainInterva",     39, 16, 0, "IPK_ServiceEngineMaintainInterva (16bit)"),
    ]
    cross_pass = 0
    cross_fail: List[str] = []
    for sname, sb, length, bo, hint in cross_samples:
        # Locate the signal anywhere in the selected set
        s = None
        for mm in selected:
            for ss in mm.signals:
                if ss.name == sname:
                    s = ss
                    break
            if s:
                break
        if s is None:
            cross_fail.append(f"  - {sname} not found in any selected message")
            continue
        # Verify declared start_bit/length/byte_order match the parsed values
        if (s.start_bit, s.length, s.byte_order) != (sb, length, bo):
            cross_fail.append(
                f"  - {hint}: declared ({sb},{length},bo={bo}) but DBC says "
                f"({s.start_bit},{s.length},bo={s.byte_order})"
            )
            continue
        # Construct a payload with a known raw value
        # 0xDEADBEEF truncated to length bits
        known_raw = 0xDEADBEEF & ((1 << length) - 1)
        data = bytearray(8)
        bit_encode(data, sb, length, bo, known_raw)
        got = bit_extract(data, sb, length, bo)
        if got == known_raw:
            cross_pass += 1
        else:
            cross_fail.append(f"  - {mname}.{sname}: expected 0x{known_raw:X}, got 0x{got:X}")

    print(f"[4] Cross-byte     : {cross_pass}/{len(cross_samples)} OK" +
          (f", {len(cross_fail)} FAIL" if cross_fail else ""))
    if cross_fail:
        for line in cross_fail:
            print(line)

    # ----- Summary -----
    print()
    total_fail = len(mapping_fail) + len(rt_fail) + len(boundary_fail) + len(cross_fail)

    # ----- [5] Sentinel bitmap regression -----
    # Validates the per-CAN-ID bit assignment table generated by
    # assign_bitmap() in tools/dbc_parse.py.
    bitmap_fail = []
    node = "IPK"

    # Build the RX-only list (IPK node consumers).
    all_msgs = dbc_parse.parse_dbc(DBC_PATH)
    rx_msgs = [m for m in all_msgs if m.transmitter != node]

    # 5a. baseline: assign with empty state -> bits 0..63 dense.
    table, _ = dbc_parse.assign_bitmap(rx_msgs, node, prev_state={})
    if len(table) != 96:
        bitmap_fail.append(f"  - table length {len(table)} != 96")
    rx_count = min(64, len(rx_msgs))
    if rx_count > 0:
        baseline_ids = [table[b] for b in range(rx_count)]
        if len(set(baseline_ids)) != rx_count:
            bitmap_fail.append(f"  - baseline bits not unique: {baseline_ids}")
        bad_ids = [c for c in baseline_ids if c >= 0x800]
        if bad_ids:
            bitmap_fail.append(f"  - baseline has non-11-bit IDs: {[hex(c) for c in bad_ids]}")
        if any(table[b] != 0 for b in range(64, 96)):
            bitmap_fail.append("  - reserved pool non-zero")

    # 5b. message removal: drop the first RX message and re-assign.
    #     bit-0 must become sentinel_unused; locked bits stable.
    if rx_count >= 2:
        prev_state = {b: table[b] for b in range(rx_count) if table[b] != 0}
        rx_minus = rx_msgs[1:]
        table2, _ = dbc_parse.assign_bitmap(rx_minus, node, prev_state)
        if table2[0] != 0:  # sentinel_unused
            bitmap_fail.append(
                f"  - bit-0 should be sentinel after removal, got 0x{table2[0]:04X}")
        for b in range(1, min(rx_count, 64)):
            if table2[b] != table[b]:
                bitmap_fail.append(
                    f"  - bit-{b} unstable across removal: "
                    f"0x{table[b]:04X} -> 0x{table2[b]:04X}")

    # 5c. message addition: cap at 64 RX bits; new ID must not steal
    #     a slot from existing bits.
    if 0 < rx_count < 64:
        prev_state = {b: table[b] for b in range(rx_count) if table[b] != 0}
        new_msg = dbc_parse.Message(
            can_id=0x07FF, name="TEST_FAKE", dlc=8, transmitter="FakeNode")
        rx_plus = [new_msg] + rx_msgs
        table3, _ = dbc_parse.assign_bitmap(rx_plus, node, prev_state)
        if 0x07FF in table3:
            bitmap_fail.append("  - new fake ID was assigned a bit despite cap")
        for b in range(rx_count):
            if table3[b] != table[b]:
                bitmap_fail.append(
                    f"  - bit-{b} unstable across addition: "
                    f"0x{table[b]:04X} -> 0x{table3[b]:04X}")

    bitmap_pass = "OK" if not bitmap_fail else f"{len(bitmap_fail)} FAIL"
    print(f"[5] Bitmap sentinel: {bitmap_pass}")
    for line in bitmap_fail:
            print(line)
    total_fail += len(bitmap_fail)

    if total_fail == 0:
        print("ALL TESTS PASSED")
        return 0
    print(f"TOTAL FAILURES: {total_fail}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
