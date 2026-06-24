#!/usr/bin/env bash
# =============================================================================
# validate_map.sh — 校验 Common/map/{mapId}/ 必填 JSON 字段
#
# 用法：
#   ./tools/map_export/validate_map.sh Common/map
#   ./tools/map_export/validate_map.sh Common/map/1001
# =============================================================================

set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

fail() { echo -e "${RED}[validate_map]${NC} $*" >&2; exit 1; }
ok()   { echo -e "${GREEN}[validate_map]${NC} $*"; }

lookup_map_config_version() {
    local mapId="$1"
    local configFile="database/map_config.lua"
    [[ -f "$configFile" ]] || return 0
    local line
    line=$(grep -E "^\s*\[${mapId}\]\s*=" "$configFile" || true)
    [[ -n "$line" ]] || fail "map_config.lua 缺少 mapId=${mapId}"
    local version
    version=$(awk -v id="$mapId" '
        $0 ~ "\\[" id "\\]" { inRow=1 }
        inRow && /version =/ { gsub(/[^0-9]/, "", $3); print $3; exit }
    ' "$configFile")
    [[ -n "$version" ]] || fail "map_config.lua mapId=${mapId} 缺少 version"
    echo "$version"
}

validate_one() {
    local dir="$1"
    local meta="${dir}/meta.json"
    [[ -f "$meta" ]] || fail "缺少 meta.json: $dir"

    for key in mapId version; do
        grep -q "\"${key}\"" "$meta" || fail "${meta}: 缺少字段 ${key}"
    done

    local mapId
    mapId=$(grep -o '"mapId"[[:space:]]*:[[:space:]]*[0-9]\+' "$meta" | grep -o '[0-9]\+$' | head -n1)
    [[ -n "$mapId" ]] || fail "${meta}: 无法解析 mapId"

    local metaVersion
    metaVersion=$(grep -o '"version"[[:space:]]*:[[:space:]]*[0-9]\+' "$meta" | grep -o '[0-9]\+$' | head -n1)
    [[ -n "$metaVersion" ]] || fail "${meta}: 无法解析 version"

    if [[ -f "database/map_config.lua" ]]; then
        local cfgVersion
        cfgVersion=$(lookup_map_config_version "$mapId")
        [[ "$cfgVersion" == "$metaVersion" ]] || fail "mapId=${mapId} version 不一致: meta=${metaVersion} config=${cfgVersion}"
    fi

    if [[ -f "${dir}/spawns.json" ]]; then
        grep -q '"x"' "${dir}/spawns.json" || fail "${dir}/spawns.json: 缺少 x"
    fi

    ok "OK ${dir}"
}

TARGET="${1:-Common/map}"
if [[ ! -d "$TARGET" ]]; then
    fail "目录不存在: $TARGET"
fi

if [[ -f "${TARGET}/meta.json" ]]; then
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
