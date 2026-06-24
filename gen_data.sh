#!/usr/bin/env bash
# =============================================================================
#  gen_data.sh —  Common/DataDoc Excel → database Lua 配表生成
#
#  用法：
#    ./gen_data.sh           # 转换 Common/DataDoc/*.xlsx
#    ./gen_data.sh --init    # 仅生成示例 Excel（不转换）
#
#  依赖：Python 3 + openpyxl（见 tools/requirements-datadoc.txt）
# =============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REQ_FILE="$SCRIPT_DIR/tools/requirements-datadoc.txt"
GEN_PY="$SCRIPT_DIR/tools/gen_datadoc.py"

if ! python3 -c "import openpyxl" 2>/dev/null; then
    echo "[gen_data] installing openpyxl..."
    if ! python3 -m pip install -q -r "$REQ_FILE" 2>/dev/null; then
        echo "[gen_data] 请先安装: python3-pip 或 pip3 install -r tools/requirements-datadoc.txt" >&2
        exit 1
    fi
fi

exec python3 "$GEN_PY" "$@"
