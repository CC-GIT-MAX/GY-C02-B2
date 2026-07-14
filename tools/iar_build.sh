#!/usr/bin/env bash
# tools/iar_build.sh
# ------------------
# Drive an IAR EWARM 9.x command-line build of the C02-B2 FLASH target.
#
# Usage:
#   tools/iar_build.sh                  # build FLASH config
#   tools/iar_build.sh --make            # re-run ewp_add_files.py first
#
# Requirements:
#   - IAR Embedded Workbench for ARM 9.30+ installed
#   - iarbuild on PATH (or set IAR_BIN below)
#
# On hosts without IAR, the script prints a friendly message and
# exits 0 so it can be wired into CI without breaking the loop.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
EWP="${REPO_ROOT}/EWARM/C02_B2.ewp"
CONFIG="FLASH"
IAR_BIN="${IAR_BIN:-iarbuild}"

if [[ "${1:-}" == "--make" ]]; then
    echo "[iar_build] running ewp_add_files.py --apply first"
    python "${REPO_ROOT}/tools/ewp_add_files.py" --apply
fi

if ! command -v "${IAR_BIN}" >/dev/null 2>&1; then
    cat <<EOF
[iar_build] IAR command-line not found (\$IAR_BIN=$IAR_BIN).

This is normal on a workstation that only has Git / Python. To run
a real IAR build, install IAR EWARM for ARM 9.30+ and either:

  * put  <install>/common/bin  on PATH, or
  * set IAR_BIN=/path/to/iarbuild

Expected layout:
  "C:/Program Files/IAR Systems/Embedded Workbench 9.40/common/bin/iarbuild.exe"

Project file: ${EWP}
Config:       ${CONFIG}

After installation:
  tools/iar_build.sh --make
EOF
    exit 0
fi

echo "[iar_build] ${IAR_BIN} ${EWP} -${CONFIG} -build ${CONFIG}"
exec "${IAR_BIN}" "${EWP}" "-${CONFIG}" -build "${CONFIG}"