#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""tools/ewp_add_files.py - idempotent ewp augmentation."""
from __future__ import annotations
import argparse, re, sys
from pathlib import Path
REPO_ROOT = Path(__file__).resolve().parent.parent
EWP_PATH  = REPO_ROOT / "EWARM" / "C02_B2.ewp"
APP_SUBGROUPS = [
    ("can", ["can_db","can_db_codec","can_db_ipk_gen","can_if","can_rx","can_tx"]),
    ("init", ["bsp_init","drv_init"]),
    ("log", ["log"]),
    ("rti", ["rti"]),
    ("scheduler", ["scheduler"]),
    ("signal", ["signal","signal_test"]),
    ("storage", ["kv"]),
    ("mod_template", ["mod_template"]),
]
APP_TOP_HEADERS = ["result.h","types.h"]
def render_subgroup(sub, stubs):
    files = []
    for stub in stubs:
        for ext in (".c",".h"):
            rel = "app/" + sub + "/" + stub + ext
            if (REPO_ROOT / rel).is_file(): files.append(rel)
    files.sort()
    if not files: return ""
    out = ["    <group>", "      <name>" + sub + "</name>"]
    for rel in files:
        out.append("      <file>")
        out.append("        <name>$PROJ_DIR$/../" + rel + "</name>")
        out.append("      </file>")
    out.append("    </group>")
    return chr(10).join(out)
def render_app_top_headers():
    out = []
    for h in APP_TOP_HEADERS:
        rel = "app/" + h
        if (REPO_ROOT / rel).is_file():
            out.append("    <file>")
            out.append("      <name>$PROJ_DIR$/../" + rel + "</name>")
            out.append("    </file>")
    return chr(10).join(out)
def collect_includes():
    out = ["$PROJ_DIR$/../app"]
    for sub, _ in APP_SUBGROUPS:
        out.append("$PROJ_DIR$/../app/" + sub)
    return out
RE_APP_GROUP = re.compile(
    r"(<group>\s*<name>app</name>\s*<file>\s*<name>\$PROJ_DIR\$/\.\./app/main\.c</name>\s*</file>)(\s*</group>)",
    re.DOTALL)
RE_CCINCLUDE_OPTION = re.compile(
    r"(<option>\s*<name>CCIncludePath2</name>)(.*?)(</option>)",
    re.DOTALL,
)
RE_STATE_LINE = re.compile(r"<state>([^<]*)</state>")
def patch_app_group(text):
    m = RE_APP_GROUP.search(text)
    if not m: return text, [], []
    head_end = m.end(1); blocks = []; added = []; skipped = []
    for sub, stubs in APP_SUBGROUPS:
        rels = []
        for stub in stubs:
            for ext in (".c",".h"):
                rel = "app/" + sub + "/" + stub + ext
                if (REPO_ROOT / rel).is_file(): rels.append(rel)
        rels.sort()
        if not rels: continue
        if all("$PROJ_DIR$/../" + r in text for r in rels):
            skipped.append(sub + "/ (" + str(len(rels)) + " files)")
            continue
        blocks.append(render_subgroup(sub, stubs))
        added.extend(rels)
    top_added = []
    for h in APP_TOP_HEADERS:
        rel = "app/" + h
        if (REPO_ROOT / rel).is_file():
            if "$PROJ_DIR$/../" + rel in text: skipped.append(rel)
            else: top_added.append(rel)
    if top_added:
        blocks.append(render_app_top_headers())
        added.extend(top_added)
    if not blocks: return text, [], skipped
    insertion = chr(10) + chr(10).join(blocks) + chr(10) + "  "
    new = text[:head_end] + insertion + text[m.start(2):]
    return new, added, skipped
def patch_ccinclude(text):
    m = RE_CCINCLUDE_OPTION.search(text)
    if not m: return text, [], []
    body = m.group(2)
    existing = set()
    for sm in RE_STATE_LINE.finditer(body):
        existing.add(sm.group(1))
    to_add = [p for p in collect_includes() if p not in existing]
    if not to_add: return text, [], ["CCIncludePath2 (already complete)"]
    extra = ""
    for p in to_add:
        extra += chr(10) + "          <state>" + p + "</state>"
    new = text[: m.end(2)] + extra + text[m.start(3):]
    return new, to_add, []
def diff_preview(before, after, limit=120):
    import difflib
    diff = list(difflib.unified_diff(
        before.splitlines(keepends=True),
        after.splitlines(keepends=True),
        fromfile="C02_B2.ewp", tofile="C02_B2.ewp", n=1))
    head = "".join(diff[:limit])
    more = "" if len(diff) <= limit else chr(10) + "... (" + str(len(diff)-limit) + " more lines)"
    return head + more
def main():
    p = argparse.ArgumentParser(description="Augment EWARM/C02_B2.ewp")
    p.add_argument("--apply", action="store_true")
    p.add_argument("--check", action="store_true")
    args = p.parse_args()
    if not EWP_PATH.is_file():
        print("ERROR: " + str(EWP_PATH) + " not found", file=sys.stderr)
        return 2
    before = EWP_PATH.read_text(encoding="utf-8")
    after_f, added_f, skipped_f = patch_app_group(before)
    after_i, added_i, skipped_i = patch_ccinclude(after_f)
    added = added_f + added_i
    skipped = skipped_f + skipped_i
    changed = (after_i != before)
    print("EWP: " + str(EWP_PATH))
    print("  files added:     " + str(len(added_f)))
    for r in added_f: print("    + " + r)
    print("  includes added:  " + str(len(added_i)))
    for r in added_i: print("    + " + r)
    print("  skipped (already present): " + str(len(skipped)))
    for r in skipped: print("    = " + r)
    if args.check: return 1 if added else 0
    if not changed: print("No changes needed."); return 0
    if not args.apply:
        print(chr(10) + "--- DRY RUN (pass --apply to write) ---")
        print(diff_preview(before, after_i)); return 0
    EWP_PATH.write_text(after_i, encoding="utf-8")
    print(chr(10) + "Wrote " + str(EWP_PATH) + " (" + str(len(after_i)-len(before)) + " bytes).")
    return 0
if __name__ == "__main__": sys.exit(main())