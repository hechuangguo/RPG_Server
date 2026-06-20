#!/usr/bin/env bash
# check_common_headers.sh — 校验 Server 对 Common 子模块的 #include 与头文件语法
#
# 用法：./scripts/check_common_headers.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
COMMON="${ROOT}/Common"
FAIL=0

echo "=== Common include paths (Server source) ==="
while IFS= read -r -d '' f; do
    while IFS= read -r inc; do
        rel="${inc#../Common/}"
        rel="${rel#../../Common/}"
        target="${COMMON}/${rel}"
        if [[ ! -f "${target}" ]]; then
            echo "MISSING: ${f} -> ${inc} (${target})"
            FAIL=1
        fi
    done < <(grep -E '#include\s+"(\.\./)+Common/[^"]+"' "${f}" | sed -E 's/.*#include "([^"]+)".*/\1/' || true)
done < <(find "${ROOT}" -type f \( -name '*.cpp' -o -name '*.h' \) \
    ! -path '*/.build/*' ! -path '*/3Party/*' ! -path '*/.cursor/*' -print0)

echo "=== Common/*.h syntax-only (g++) ==="
if ! command -v g++ >/dev/null 2>&1; then
    echo "SKIP: g++ not found"
else
    for hdr in "${COMMON}"/*.h; do
        [[ -f "${hdr}" ]] || continue
        base="$(basename "${hdr}")"
        if ! g++ -std=c++17 -fsyntax-only -I "${COMMON}" "${hdr}" 2>/dev/null; then
            echo "SYNTAX FAIL: ${base}"
            g++ -std=c++17 -fsyntax-only -I "${COMMON}" "${hdr}" 2>&1 | tail -3 || true
            FAIL=1
        fi
    done
fi

echo "=== ClientMsg.h aggregate present ==="
if [[ ! -f "${COMMON}/ClientMsg.h" ]]; then
    echo "MISSING: Common/ClientMsg.h (deprecated aggregate)"
    FAIL=1
else
    echo "OK: Common/ClientMsg.h"
fi

if [[ "${FAIL}" -eq 0 ]]; then
    echo "PASS: Common headers check"
else
    echo "FAIL: Common headers check"
    exit 1
fi
