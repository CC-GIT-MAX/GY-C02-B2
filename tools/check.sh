#!/usr/bin/env bash
#
# check.sh - 提交前自检（也可在 CI 中调用）
#
# 检查项：
#   1. 业务代码 .c 中禁止出现 extern 变量声明
#      例外：scheduler.c 中引用 mod_xxx 描述符
#   2. 业务代码禁止直接 include 厂商驱动头（仅中间件层可）
#   3. 业务代码禁止直接调用 printf（必须用 LOG_*）
#   4. .clang-format 干跑，检查格式（需要 clang-format）
#
# 退出码：非零 = 至少一项违规
#
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APP_DIR="$ROOT/app"
VIOLATIONS=0

report() {
    if [ "$1" -ne 0 ]; then
        echo "[FAIL] $2 ($1 处)"
        VIOLATIONS=$((VIOLATIONS + 1))
    else
        echo "[ OK ] $2"
    fi
}

# 1. extern 在 app/ 的 .c 中（排除 scheduler.c）
cnt=$(grep -rEn "^[[:space:]]*extern[[:space:]]+[A-Za-z_]" \
        --include="*.c" "$APP_DIR" 2>/dev/null \
        | grep -v "app/scheduler/scheduler.c" | wc -l | tr -d " ")
report "$cnt" "extern in app/**/*.c (excluding scheduler.c)"

# 2. 业务代码 include 驱动头
cnt=$(grep -rEn "#include[[:space:]]+\"(flexcan_driver|adc_driver|spi_driver|i2c_driver|linflexd_uart_driver|dma_driver|etmr_pwm_driver|pins_driver|interrupt_manager|power_manager|clock_manager|wdg_driver|flash_driver|lptmr_driver)\.h\"" \
        --include="*.c" --include="*.h" "$APP_DIR" 2>/dev/null | wc -l | tr -d " ")
report "$cnt" "driver headers included by app/"

# 3. printf 出现在业务代码（应走 LOG_*）
#    例外：log/log.c 自身需要它
cnt=$(grep -rEn "\bprintf[[:space:]]*\(" \
        --include="*.c" "$APP_DIR" 2>/dev/null \
        | grep -v "app/log/log.c" | wc -l | tr -d " ")
report "$cnt" "raw printf() in app/"

# 4. clang-format 检查（可选）
if command -v clang-format >/dev/null 2>&1; then
    bad=0
    while IFS= read -r f; do
        diff <(clang-format --style=file "$f") "$f" >/dev/null 2>&1 || bad=$((bad + 1))
    done < <(find "$APP_DIR" -name "*.c" -o -name "*.h")
    if [ "$bad" -eq 0 ]; then
        echo "[ OK ] clang-format"
    else
        echo "[WARN] clang-format: $bad file(s) not formatted (non-fatal)"
    fi
else
    echo "[SKIP] clang-format not installed"
fi

echo
if [ "$VIOLATIONS" -gt 0 ]; then
    echo "FAILED: $VIOLATIONS 类别违规"
    exit 1
fi
echo "PASSED"
exit 0
