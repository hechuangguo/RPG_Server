#!/usr/bin/env bash
# =============================================================================
# build_protobuf.sh — 离线编译或链接 3Party protobuf（protoc + libprotobuf.a）
# =============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=versions.env
source "${SCRIPT_DIR}/versions.env"

PREFIX="${SCRIPT_DIR}/protobuf"
BIN="${PREFIX}/bin/protoc"
LIB="${PREFIX}/lib/libprotobuf.a"

if [[ -f "${LIB}" && -x "${BIN}" ]]; then
    echo "[protobuf] 已存在 ${BIN}，跳过"
    exit 0
fi

if command -v protoc >/dev/null 2>&1; then
    echo "[protobuf] 使用系统 protoc"
    mkdir -p "${PREFIX}/bin" "${PREFIX}/lib" "${PREFIX}/include"
    PROTOC_PATH="$(command -v protoc)"
    if ! cp -f "${PROTOC_PATH}" "${BIN}" 2>/dev/null; then
        if ! ln -sf "${PROTOC_PATH}" "${BIN}"; then
            echo "[protobuf] 错误: 无法将系统 protoc 安装到 ${BIN}（cp 与 ln 均失败）" >&2
            exit 1
        fi
    fi
    if ! chmod +x "${BIN}"; then
        echo "[protobuf] 错误: 无法为 ${BIN} 设置可执行权限" >&2
        exit 1
    fi
    if [[ ! -x "${BIN}" ]]; then
        echo "[protobuf] 错误: ${BIN} 不可执行" >&2
        exit 1
    fi
    for libpath in /usr/local/lib/libprotobuf.a /usr/lib64/libprotobuf.a; do
        if [[ -f "${libpath}" ]]; then
            if cp -f "${libpath}" "${LIB}" 2>/dev/null || ln -sf "${libpath}" "${LIB}"; then
                break
            fi
            echo "[protobuf] 警告: 无法链接 ${libpath} -> ${LIB}" >&2
        fi
    done
    for inc in /usr/local/include /usr/include; do
        if [[ -d "${inc}/google/protobuf" ]]; then
            ln -sfn "${inc}/google" "${PREFIX}/include/google" 2>/dev/null \
                || echo "[protobuf] 警告: 无法链接头文件 ${inc}/google" >&2
            break
        fi
    done
    if [[ -f "${LIB}" && -x "${BIN}" ]]; then
        echo "[protobuf] 链接完成: ${BIN}"
        exit 0
    fi
    echo "[protobuf] 系统 libprotobuf.a 未找到，尝试 vendor 编译..."
fi

VENDOR_TGZ="${SCRIPT_DIR}/vendor/${PROTOBUF_VENDOR_TGZ:-protobuf-cpp-3.${PROTOBUF_VERSION}.tar.gz}"
if [[ ! -f "${VENDOR_TGZ}" ]]; then
    # 兼容旧命名 protobuf-cpp-21.12.tar.gz
    VENDOR_TGZ="${SCRIPT_DIR}/vendor/protobuf-cpp-${PROTOBUF_VERSION}.tar.gz"
fi
if [[ ! -f "${VENDOR_TGZ}" ]]; then
    echo "[protobuf] 未找到 vendor 包（${PROTOBUF_VENDOR_TGZ:-protobuf-cpp-3.${PROTOBUF_VERSION}.tar.gz}）且无系统 libprotobuf.a" >&2
    echo "[protobuf] 请执行: ./3Party/fetch_vendor.sh  或  sudo dnf install protobuf protobuf-devel" >&2
    exit 1
fi

BUILD_DIR="${SCRIPT_DIR}/src/protobuf-build"
rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"
tar --no-same-owner -xzf "${VENDOR_TGZ}" -C "${BUILD_DIR}" --strip-components=1

cmake -S "${BUILD_DIR}" -B "${BUILD_DIR}/out" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${PREFIX}" \
    -Dprotobuf_BUILD_TESTS=OFF \
    -Dprotobuf_BUILD_SHARED_LIBS=OFF

cmake --build "${BUILD_DIR}/out" --parallel "$(nproc 2>/dev/null || echo 4)"
cmake --install "${BUILD_DIR}/out"
# cmake install 在部分平台写入 lib64/，与 CMakeLists 期望的 lib/ 对齐
if [[ -f "${PREFIX}/lib64/libprotobuf.a" && ! -f "${LIB}" ]]; then
    mkdir -p "${PREFIX}/lib"
    ln -sf ../lib64/libprotobuf.a "${LIB}"
fi
echo "[protobuf] 安装完成: ${BIN}"
