#!/usr/bin/env bash
# check_common_headers.sh — 校验 Server 不引用已删除的 Common 遗留头文件
#
# 用法：./scripts/check_common_headers.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
COMMON="${ROOT}/Common"
FAIL=0

echo "=== Common/*.proto present ==="
for f in ClientCommon.proto WireCommon.proto LoginMsg.proto; do
    if [[ ! -f "${COMMON}/${f}" ]]; then
        echo "MISSING: ${COMMON}/${f}"
        FAIL=1
    fi
done

echo "=== Server must not include deleted Common legacy headers ==="
LEGACY_INC='Common/(ClientTypes|NetDefine|MsgId|LoginMsg|LoginCommon|MapDataMsg|MapDataCommon|ChatMsg|ChatCommon|ZoneMsg|ZoneCommon|ClientMsg|PropertyMsg|PropertyCommon|EquipMsg|EquipCommon|SpellMsg|SpellCommon|RelationMsg|RelationCommon|GoldMsg|GoldCommon|generated)\.h'
while IFS= read -r hit; do
    [[ -z "${hit}" ]] && continue
    echo "LEGACY INCLUDE: ${hit}"
    FAIL=1
done < <(grep -R --include='*.cpp' --include='*.h' -n -E "#include.*${LEGACY_INC}" "${ROOT}" \
    --exclude-dir='.build' --exclude-dir='3Party' --exclude-dir='.cursor' 2>/dev/null || true)

if [[ "${FAIL}" -eq 0 ]]; then
    echo "PASS: Common headers check"
else
    echo "FAIL: Common headers check"
    exit 1
fi
