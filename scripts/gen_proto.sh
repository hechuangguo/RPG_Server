#!/usr/bin/env bash
# =============================================================================
# gen_proto.sh — 从 Common/*.proto 生成 Server C++ 代码到 Protobuf/
#
# 用法：./scripts/gen_proto.sh（Build.sh / autoinit.sh 自动调用）
# 真源：Common/ 子模块（*.proto）
# 输出：Protobuf/*.pb.h、Protobuf/*.pb.cc（禁止手改）
# 增量：生成物存在且 proto 未更新时跳过 protoc（避免 Build.sh 每次全量编译）
# =============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
COMMON_DIR="${PROJECT_DIR}/Common"
OUT_CPP="${PROJECT_DIR}/Protobuf"

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
    local build="${PROJECT_DIR}/3Party/build_protobuf.sh"
    if [[ -x "${build}" ]]; then
        echo "[gen_proto] protoc 未找到，尝试 ${build} ..." >&2
        if "${build}" && [[ -x "${bundled}" ]]; then
            echo "${bundled}"
            return
        fi
    fi
    echo "错误: 未找到 protoc（可执行 ./3Party/build_protobuf.sh 或安装系统 protobuf 开发包）" >&2
    exit 1
}

PROTOC_BIN="$(resolve_protoc)"
step() { echo "[gen_proto] $*"; }

mkdir -p "${OUT_CPP}"

PROTO_FILES=(
    ClientCommon.proto
    WireCommon.proto
    LoginCommon.proto
    LoginMsg.proto
    MapDataCommon.proto
    MapDataMsg.proto
    ZoneCommon.proto
    ZoneMsg.proto
    ChatCommon.proto
    ChatMsg.proto
    SystemCommon.proto
    SystemMsg.proto
    NpcCommon.proto
    NpcMsg.proto
)

missing_proto=()
for f in "${PROTO_FILES[@]}"; do
    [[ -f "${COMMON_DIR}/${f}" ]] || missing_proto+=("${f}")
done
if [[ ${#missing_proto[@]} -gt 0 ]]; then
    echo "错误: Common 缺少 proto 文件: ${missing_proto[*]}" >&2
    echo "主仓已迁移 Protobuf/，Common 子模块须同步到含 *.proto 的版本。" >&2
    echo "请执行: ./pull.sh  或  git submodule update --init --recursive" >&2
    echo "若本地有未提交改动: cd Common && git checkout main && git pull" >&2
    exit 1
fi

proto_needs_regen() {
    local proto_name="$1"
    local base="${proto_name%.proto}"
    local proto_path="${COMMON_DIR}/${proto_name}"
    local pb_h="${OUT_CPP}/${base}.pb.h"
    local pb_cc="${OUT_CPP}/${base}.pb.cc"

    [[ -f "${pb_h}" && -f "${pb_cc}" ]] || return 0
    [[ "${proto_path}" -nt "${pb_h}" || "${proto_path}" -nt "${pb_cc}" ]]
}

stale_proto=()
for f in "${PROTO_FILES[@]}"; do
    if proto_needs_regen "${f}"; then
        stale_proto+=("${f}")
    fi
done

if [[ ${#stale_proto[@]} -eq 0 ]]; then
    step "已是最新，跳过"
    exit 0
fi

step "protoc=$(${PROTOC_BIN} --version)"
step "生成 C++ → ${OUT_CPP}（${#stale_proto[@]}/${#PROTO_FILES[@]} 个 proto 需更新）"
# 任一 proto 过期则全量生成，保证 import 依赖一致
"${PROTOC_BIN}" -I "${COMMON_DIR}" \
    --cpp_out="${OUT_CPP}" \
    "${PROTO_FILES[@]/#/${COMMON_DIR}/}"

step "完成"
echo "GEN_PROTO_GENERATED=1"
