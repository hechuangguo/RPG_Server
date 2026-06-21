#!/usr/bin/env bash
# =============================================================================
# check_common_proto.sh — Common/*.proto 注释冒烟 + Protobuf/ 生成物检查
#
# 用法：./scripts/check_common_proto.sh
# Build.sh 在编译前调用；失败则阻断构建。
# =============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
COMMON_DIR="${SCRIPT_DIR}/Common"
GEN_CPP="${SCRIPT_DIR}/Protobuf"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

fail() { echo -e "${RED}[proto-check]${NC} $*" >&2; exit 1; }
ok()   { echo -e "${GREEN}[proto-check]${NC} $*"; }
warn() { echo -e "${YELLOW}[proto-check]${NC} $*"; }

if [[ ! -d "${COMMON_DIR}" ]]; then
    warn "Common/ 子模块未初始化，跳过 proto 检查"
    exit 0
fi

shopt -s nullglob
proto_files=("${COMMON_DIR}"/*.proto)
if [[ ${#proto_files[@]} -eq 0 ]]; then
    warn "无 .proto 文件，跳过"
    exit 0
fi

for f in "${proto_files[@]}"; do
    base="$(basename "$f")"
    if ! grep -q '@file' "$f" 2>/dev/null; then
        fail "${base}: 缺少文件头 @file 注释"
    fi
    if ! grep -q '@brief' "$f" 2>/dev/null; then
        fail "${base}: 缺少文件头 @brief 注释"
    fi
done

for f in LoginMsg.proto MapDataMsg.proto ZoneMsg.proto ChatMsg.proto SystemMsg.proto NpcMsg.proto; do
    path="${COMMON_DIR}/${f}"
    [[ -f "$path" ]] || continue
    if ! grep -qE '^message ' "$path"; then
        fail "${f}: 缺少 message 定义"
    fi
done

if [[ ! -d "${GEN_CPP}" ]]; then
    fail "Protobuf/ 不存在，请先运行 ./scripts/gen_proto.sh"
fi

for f in ClientCommon.pb.h WireCommon.pb.h MapDataMsg.pb.h LoginMsg.pb.h ZoneMsg.pb.h ChatMsg.pb.h SystemMsg.pb.h NpcMsg.pb.h; do
    if [[ ! -f "${GEN_CPP}/${f}" ]]; then
        fail "缺少生成文件 ${GEN_CPP}/${f}，请运行 ./scripts/gen_proto.sh"
    fi
done

for gen in "${GEN_CPP}"/*.pb.h; do
    [[ -f "$gen" ]] || continue
    for pf in "${proto_files[@]}"; do
        if [[ "$pf" -nt "$gen" ]]; then
            fail "生成物过期: $(basename "$gen")，请运行 ./scripts/gen_proto.sh"
        fi
    done
done

ok "Common Protobuf 检查通过 (${#proto_files[@]} 个 .proto)"
