#!/usr/bin/env bash
# Force UTF-8 locale so wc -m counts chars (not bytes) on
# Git for Windows + PowerShell combo where default LANG='''' leaks.
export LC_ALL="C.UTF-8"
export LANG="C.UTF-8"

# =============================================================================
# @brief   Commit message lint for the C02-B2 project.
# @brief   按 docs/COMMIT_CONVENTION.md 规则检查 commit message.
#
# @details 用途:
#   1) pre-commit hook: .git/hooks/commit-msg  -> tools/check_commit_msg.sh "$1"
#   2) CI / Actions:     tools/check_commit_msg.sh < commit_msg_file
#
# @details 规则概要:
#   R1 type 白名单: feat|fix|refactor|docs|test|build|ci|chore|perf|style
#   R2 subject <= 30 字符 (中文 1 char / 半角 1 char, UTF-8 char length = 字符数)
#   R3 subject 全部 UTF-8 中文字符 (允许 v0.5 / KEEP_LAST / DBC 等专有名词半角)
#   R4 subject 禁用句号 (\u3002 中文句号 / . 英文句号)
#   R5 body 若非空, 必须至少一行以 "- " 开头
#   R6 body 每行 <= 100 字符
#   R7 整段描述 (subject+body+footer) 总字符 <= 300
#       例外: 改动 > 10 文件或新增 > 5 文件允许 <= 600, 但 body 第一段必须是
#             < 100 字符的动机说明, 否则拒绝.
#   R8 代号/缩写检测: subject 命中 \b(F\+B\+Ever|KISS|DDD|TBD|RFC)\b 拒绝
#       (无引用即无含义的"会话内部"代号; 在 body 有"Refs:" 字样可豁免)
#
# @return  exit 0 pass, non-zero fail
# =============================================================================
set -u

MSG_FILE="${1:-.git/COMMIT_EDITMSG}"
if [ ! -f "$MSG_FILE" ]; then
    echo "[check_commit_msg] message file not found: $MSG_FILE" >&2
    exit 2
fi

MSG=$(cat "$MSG_FILE")
# Strip comment lines (start with #).
MSG_CLEAN=$(echo "$MSG" | grep -v -E '^#' || true)
TOTAL=$(printf '%s' "$MSG_CLEAN" | wc -m | tr -d ' ')

# ----- subject: first line -----
SUBJECT=$(echo "$MSG_CLEAN" | head -n1)
SUBJ_LEN=$(printf '%s' "$SUBJECT" | wc -m | tr -d ' ')

# ----- body: after first blank line -----
BODY=$(echo "$MSG_CLEAN" | awk 'BEGIN{b=0} /^$/{b=1; next} b{print}')
BODY_FIRST_PARA_LEN=$(echo "$BODY" | awk 'BEGIN{p=""} /^[[:space:]]*-/{exit} {p=p $0; if (length(p)>0 && substr(p,length(p),1)=="\n") exit} END{print length(p)}')

# ----- merge/revert 豁免 -----
# git merge / git revert 自动生成 subject 以 Merge/Revert 开头, 无 type 前缀, 放行.
if echo "$SUBJECT" | grep -qE '^(Merge|Revert)[[:space:]]'; then
    echo "[ok] merge/revert auto commit exempt (subject=$SUBJ_LEN char)" >&2
    exit 0
fi

# ----- R1 type -----
if ! echo "$SUBJECT" | grep -qE '^(.+\s)?(feat|fix|refactor|docs|test|build|ci|chore|perf|style)(\([^)]+\))?:' && ! echo "$SUBJECT" | grep -qE '^(\S+ )?(feat|fix|refactor|docs|test|build|ci|chore|perf|style)(\([^)]+\))?:'; then
    echo "[FAIL] R1 type: subject 不在白名单或格式不对" >&2
    echo "       subject: $SUBJECT" >&2
    exit 1
fi

# ----- R2 subject len -----
if [ "$SUBJ_LEN" -gt 30 ]; then
    echo "[FAIL] R2 subject > 30 字符 (当前 $SUBJ_LEN): $SUBJECT" >&2
    exit 1
fi

# ----- R3 subject 中文字符 -----
# 头 5 个 (含 type+scope 的 "xxx(xx):") 是 ASCII; 之后应主要为中文 (允许 < 30% 半角)
SUBJ_AFTER_TYPE=$(echo "$SUBJECT" | sed -E 's|^([a-z]+(\([^)]+\))?:[[:space:]]+)?||')
ASCII_AFTER=$(printf '%s' "$SUBJ_AFTER_TYPE" | LC_ALL=C grep -o '[[:print:]]' | wc -m | tr -d ' ')
NONASCII_AFTER=$(printf '%s' "$SUBJ_AFTER_TYPE" | wc -m | tr -d ' ')
if [ "$NONASCII_AFTER" -gt 0 ]; then
    ASCII_PCT=$(( ASCII_AFTER * 100 / NONASCII_AFTER ))
    if [ "$ASCII_PCT" -gt 60 ]; then
        echo "[FAIL] R3 subject 主要应为中文 (当前 ASCII 占 $ASCII_PCT%): $SUBJECT" >&2
        echo "       subject 在 type 之后半角字符比例过高" >&2
        exit 1
    fi
fi

# ----- R4 subject 句号 -----
if echo "$SUBJECT" | grep -qE '[。.]$'; then
    echo "[FAIL] R4 subject 禁止句号结尾: $SUBJECT" >&2
    exit 1
fi

# ----- R5 body - 开头 -----
if [ -n "$BODY" ] && ! echo "$BODY" | grep -qE '^- '; then
    echo "[FAIL] R5 body 非空时, 必须至少一行以 '- ' 开头" >&2
    exit 1
fi

# ----- R6 body 行宽 -----
while IFS= read -r line; do
    L=$(printf '%s' "$line" | wc -m | tr -d ' ')
    if [ "$L" -gt 100 ]; then
        echo "[FAIL] R6 body 行 > 100 字符 (字符数 $L): $line" >&2
        exit 1
    fi
done < <(echo "$BODY")

# ----- R7 整段描述总字符 -----
LIMIT=300
if [ "$TOTAL" -gt "$LIMIT" ]; then
    # 例外: 文件数判断需要 git diff, 在 hook 阶段是 staged file count
    CHANGED=$(git diff --cached --name-only 2>/dev/null | wc -l | tr -d ' ')
    if [ "$CHANGED" -gt 10 ]; then
        LIMIT=600
    fi
    if [ "$TOTAL" -le "$LIMIT" ]; then
        :  # 合格
    else
        echo "[FAIL] R7 整段描述 > $LIMIT 字符 (当前 $TOTAL)。" >&2
        echo "       subject+body+footer 字符数 = $TOTAL, 限制 $LIMIT" >&2
        echo "       改动 > 10 文件时可放至 600, 但 body 第一段需 < 100 字符作动机说明" >&2
        exit 1
    fi
fi

# ----- R8 代号/缩写 -----
BANNED='\b(F\+B\+Ever|KISS|DDD|TBD|RFC)\b'
if echo "$SUBJECT" | grep -qE "$BANNED"; then
    if ! echo "$MSG_CLEAN" | grep -qE '^Refs:'; then
        echo "[FAIL] R8 subject 含会话内部代号 (F+B+Ever/KISS/DDD/TBD/RFC)" >&2
        echo "       如需引用方案文档, body 必须有 'Refs: <doc>' 行" >&2
        exit 1
    fi
fi

# ----- R7 例外: 大改动需要 body 第一段动机 < 100 字符 -----
CHANGED=$(git diff --cached --name-only 2>/dev/null | wc -l | tr -d ' ')
if [ "$CHANGED" -gt 10 ] && [ -n "$BODY" ]; then
    BODY_PARA1=$(echo "$BODY" | awk 'BEGIN{p=""} /^[[:space:]]*-/{exit} {if (p!="") p=p ORS; p=p $0} END{print p}')
    P1_LEN=$(printf '%s' "$BODY_PARA1" | wc -m | tr -d ' ')
    if [ "$P1_LEN" -gt 100 ]; then
        echo "[FAIL] R7-exc 改动 > 10 文件时, body 第一段动机必须 < 100 字符 (当前 $P1_LEN)" >&2
        exit 1
    fi
fi

echo "[ok] commit_msg 校验通过 (subject=$SUBJ_LEN char, total=$TOTAL char, files=$CHANGED)"
exit 0
