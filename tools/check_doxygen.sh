#!/usr/bin/env bash
#
# check_doxygen.sh - 包装脚本，转发到 Python 实现
# 真实逻辑在 check_doxygen.py
#
exec python "$(dirname "$0")/check_doxygen.py" "$@"
