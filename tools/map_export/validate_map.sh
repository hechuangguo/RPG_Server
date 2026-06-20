#!/usr/bin/env bash
# =============================================================================
# validate_map.sh — 校验 maps/runtime/{mapId}/ 必填 JSON 字段
#
# 用法：
#   ./tools/map_export/validate_map.sh maps/runtime/1001
#   ./tools/map_export/validate_map.sh maps/runtime
# =============================================================================

set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

fail() { echo -e "${RED}[validate_map]${NC} $*" >&2; exit 1; }
ok()   { echo -e "${GREEN}[validate_map]${NC} $*"; }

validate_one() {
    local dir="$1"
    local meta="${dir}/map.meta.json"
    [[ -f "$meta" ]] || fail "缺少 map.meta.json: $dir"

    for key in mapId version coordSystem worldBounds maxStepWalk maxStepRun; do
        grep -q "\"${key}\"" "$meta" || fail "${meta}: 缺少字段 ${key}"
    done

    for bound in minX maxX minZ maxZ; do
        grep -q "\"${bound}\"" "$meta" || fail "${meta}: worldBounds 缺少 ${bound}"
    done

    if [[ -f "${dir}/spawns.json" ]]; then
        grep -q '"name"' "${dir}/spawns.json" || fail "${dir}/spawns.json: 缺少 name"
        grep -q '"x"' "${dir}/spawns.json" || fail "${dir}/spawns.json: 缺少 x"
    fi

    ok "OK ${dir}"
}

TARGET="${1:-maps/runtime}"
if [[ ! -d "$TARGET" ]]; then
    fail "目录不存在: $TARGET"
fi

if [[ -f "${TARGET}/map.meta.json" ]]; then
    validate_one "$TARGET"
    exit 0
fi

count=0
for d in "${TARGET}"/*/; do
    [[ -d "$d" ]] || continue
    validate_one "${d%/}"
    count=$((count + 1))
done

[[ "$count" -gt 0 ]] || fail "未找到子目录: $TARGET"
ok "共校验 ${count} 张地图"
