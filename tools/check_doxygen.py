#!/usr/bin/env python3
"""Doxygen 注释自动检查

规则：
  R1. .h 中所有函数声明必须有 /** @brief 紧邻上方
  R2. .c 中所有非 static 函数必须有 /** @brief 紧邻上方
  R3. 每个函数的 Doxygen 块**必须有中文 @brief**；英文 @brief 可选（推荐保留 1 行）
  R4. .c 中 static 函数至少 1 行 /** @brief ... */
  R5. .h 中函数：有参数必须 @param；有返回值必须 @return
  R6. @details（中文）：可选；.c 中推荐补充实现细节，.h 中一般不写

退出码：非零 = 至少一项违规
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
APP_DIR = ROOT / "app"

# 匹配函数声明/定义（一行内）
FUNC_RE = re.compile(
    r"^[A-Za-z_][A-Za-z0-9_ *]*\s+"
    r"[A-Za-z_][A-Za-z0-9_]*"
    r"\s*\([^;{}]*\)"
    r"\s*([;{]|$)"
)
NAME_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*\s*\(")

# 中文范围（BMP 基础汉字 + 标点）
CJK_RE = re.compile(r"[\u4e00-\u9fff\u3000-\u303f\uff00-\uffef]")

# 匹配带参数和返回类型的函数声明（用于 R5 检查）
DECL_RE = re.compile(
    r"^([A-Za-z_][A-Za-z0-9_ *]+)\s+"
    r"([A-Za-z_][A-Za-z0-9_]*)\s*"
    r"\(([^;{}]*)\)\s*;"
)


def list_functions(path: Path) -> list[tuple[int, str, str]]:
    """返回 [(line_no, name, signature), ...]"""
    results = []
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except Exception:
        return results
    for lineno, line in enumerate(text.splitlines(), 1):
        if not FUNC_RE.match(line):
            continue
        m = NAME_RE.search(line)
        if not m:
            continue
        name = m.group(0)
        name = re.sub(r"\s*\($", "", name)
        results.append((lineno, name, line))
    return results


def get_doxygen_block(lines: list[str], func_line: int) -> str | None:
    """从 func_line 上方找到 /** ... */ 块；找不到返回 None。"""
    cur = func_line - 1
    while cur >= 1 and not lines[cur - 1].strip():
        cur -= 1
    if cur < 1:
        return None
    end_line = cur
    s = cur
    while s >= 1:
        c = lines[s - 1]
        stripped = c.lstrip()
        if stripped.startswith("/**"):
            return "\n".join(lines[s - 1:end_line])
        if not (
            stripped.startswith("/*")
            or stripped.startswith("*")
            or stripped.endswith("*/")
            or stripped == ""
        ):
            return None
        s -= 1
    return None


def count_briefs(block: str) -> tuple[int, int]:
    """返回 (english_brief_count, chinese_brief_count)。"""
    en = 0
    zh = 0
    for line in block.splitlines():
        m = re.match(r"\s*\*\s*@brief\s+(.+)", line)
        if not m:
            continue
        text = m.group(1).strip()
        if CJK_RE.search(text):
            zh += 1
        else:
            en += 1
    return en, zh


def has_chinese_brief(block: str) -> bool:
    """Return True when the Doxygen block contains at least one Chinese @brief line."""
    if not block:
        return False
    _, zh = count_briefs(block)
    return zh >= 1


def has_chinese_details(block: str) -> bool:
    """Return True when the Doxygen block has at least one @details line containing CJK."""
    if not block:
        return False
    in_details = False
    for line in block.splitlines():
        m = re.match(r"\s*\*\s*@details\b\s*(.*)", line)
        if m:
            in_details = True
            content = m.group(1).strip()
            if CJK_RE.search(content):
                return True
            continue
        if in_details:
            stripped = line.strip()
            if stripped.startswith("*") and not stripped.startswith("*/"):
                # continuation line (starts with whitespace + "*" but no new tag)
                tail = stripped.lstrip("*").strip()
                if tail and CJK_RE.search(tail):
                    return True
                if tail and not tail.startswith("@"):
                    continue
            in_details = False
    return False


def resolve_target_files(targets):
    if not targets:
        return collect_files()
    resolved = []
    for entry in targets:
        rel = entry.replace(chr(92), chr(47)).lstrip(chr(46) + chr(47))
        if not rel:
            continue
        abs_path = (ROOT / rel).resolve()
        if not abs_path.is_file():
            continue
        resolved.append(abs_path)
    return resolved

def collect_files() -> list[Path]:
    files: list[Path] = []
    for pattern in ("*.h", "*.c"):
        for p in APP_DIR.glob(pattern):
            files.append(p)
    for sub in ("scheduler", "signal", "log", "rti", "meter", "power",
                "can", "diag", "storage", "init", "mod_template"):
        sub_dir = APP_DIR / sub
        if not sub_dir.is_dir():
            continue
        for pattern in ("*.h", "*.c"):
            for p in sub_dir.glob(pattern):
                files.append(p)
    return sorted(set(files))


def check_param_return(f: Path, lines: list[str], errors: int) -> int:
    """R5: .h 文件的函数声明必须有 @param 和 @return（如适用）。"""
    if f.suffix != ".h":
        return errors
    for lineno, line in enumerate(lines, 1):
        m = DECL_RE.match(line)
        if not m:
            continue
        ret = m.group(1).strip().split()[-1]  # 取返回类型最后一个 token
        name = m.group(2)
        params = m.group(3).strip()
        # 跳过 typedef / struct
        if "typedef" in line:
            continue
        # 找 doc 块
        block = get_doxygen_block(lines, lineno)
        if block is None:
            continue  # 已经被 R1 抓了
        has_param = "@param" in block
        has_ret = "@return" in block
        if params and params != "void" and not has_param:
            print(f"[FAIL] {f}:{lineno}: {name} missing @param")
            errors += 1
        if ret != "void" and not has_ret:
            print(f"[FAIL] {f}:{lineno}: {name} missing @return")
            errors += 1
    return errors


def main(argv=None):
    if not APP_DIR.is_dir():
        print(f"app dir not found: {APP_DIR}")
        return 1

    if argv is None:
        argv = sys.argv[1:]
    files = resolve_target_files(argv)
    if argv:
        print(f"Scanning {len(files)} staged target file(s)...")
    else:
        print(f"Scanning {len(files)} files in app/...")

    errors = 0
    for f in files:
        try:
            lines = f.read_text(encoding="utf-8", errors="replace").splitlines()
        except Exception:
            continue
        # R1/R2/R3/R4
        for lineno, name, sig in list_functions(f):
            if name == "main":
                continue
            if "typedef" in sig:
                continue
            is_static = bool(re.match(r"^\s*static\s", sig))
            if f.suffix == ".c" and is_static:
                block = get_doxygen_block(lines, lineno)
                if block is None or "@brief" not in block:
                    print(f"[FAIL] {f}:{lineno}: static function {name} missing @brief")
                    errors += 1
                continue
            block = get_doxygen_block(lines, lineno)
            if block is None:
                print(f"[FAIL] {f}:{lineno}: function {name} missing @brief above")
                errors += 1
                continue
            if "@brief" not in block:
                print(f"[FAIL] {f}:{lineno}: function {name} has comment but no @brief")
                errors += 1
                continue
            en, zh = count_briefs(block)
            if zh < 1:
                print(f"[FAIL] {f}:{lineno}: function {name} missing Chinese @brief (got {en} English only)")
                errors += 1
                continue
            # R6 (advisory): @details is optional in both .c and .h; .c files are encouraged to add it.
        # R5
        errors = check_param_return(f, lines, errors)

    print()
    if errors > 0:
        print(f"FAILED: {errors} functions missing Doxygen @brief/@param/@return")
        return 1
    print("PASSED: all public/internal functions have Doxygen Chinese @brief + @param + @return")
    return 0


if __name__ == "__main__":
    sys.exit(main())
