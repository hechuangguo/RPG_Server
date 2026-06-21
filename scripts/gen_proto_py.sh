#!/usr/bin/env bash
# =============================================================================
# gen_proto_py.sh — 从 Common/*.proto 生成 Python 代码（E2E 脚本用）
#
# 用法：./scripts/gen_proto_py.sh
# 输出：scripts/pb/*_pb2.py（勿手改；需 pip install protobuf）
# =============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
COMMON_DIR="${PROJECT_DIR}/Common"
OUT_PY="${SCRIPT_DIR}/pb"

resolve_protoc() {
    if [[ -n "${PROTOC:-}" && -x "${PROTOC}" ]]; then
        echo "${PROTOC}"
        return
    fi
    local bundled="${PROJECT_DIR}/3Party/protobuf/bin/protoc"
    if [[ -x "${bundled}" ]]; then
        echo "${bundled}"
        return
    fi
    if command -v protoc >/dev/null 2>&1; then
        command -v protoc
        return
    fi
    echo "错误: 未找到 protoc" >&2
    exit 1
}

PROTOC_BIN="$(resolve_protoc)"
step() { echo "[gen_proto_py] $*"; }

mkdir -p "${OUT_PY}"

PROTO_FILES=(
    ClientCommon.proto
    WireCommon.proto
    LoginCommon.proto
    LoginMsg.proto
    MapDataCommon.proto
    MapDataMsg.proto
    SystemCommon.proto
    SystemMsg.proto
)

for f in "${PROTO_FILES[@]}"; do
    [[ -f "${COMMON_DIR}/${f}" ]] || { echo "错误: 缺少 ${COMMON_DIR}/${f}" >&2; exit 1; }
done

step "protoc → ${OUT_PY}"
"${PROTOC_BIN}" -I "${COMMON_DIR}" --python_out="${OUT_PY}" "${PROTO_FILES[@]/#/${COMMON_DIR}/}"

step "完成（${#PROTO_FILES[@]} 个 proto）"
