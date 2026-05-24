#!/usr/bin/env bash
# =============================================================================
#  download_and_build.sh — 下载并编译 3Party 依赖到本目录
#
#  依赖：
#    - Lua 5.4          → 3Party/lua/{include,lib}
#    - tinyxml2         → 3Party/tinyxml2/{include,lib}
#    - MariaDB Connector/C (MySQL 客户端 API) → 3Party/mysql/{include,lib}
#
#  用法：
#    ./3Party/download_and_build.sh
#    ./3Party/download_and_build.sh --force   # 强制重新下载编译
# =============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
SRC_DIR="${SCRIPT_DIR}/src"
CACHE_DIR="${SCRIPT_DIR}/cache"
FORCE=false

[[ "${1:-}" == "--force" ]] && FORCE=true

# shellcheck source=versions.env
source "${SCRIPT_DIR}/versions.env"

GREEN='\033[0;32m'; YELLOW='\033[1;33m'; RED='\033[0;31m'; NC='\033[0m'
step() { echo -e "${GREEN}[3Party]${NC} $*"; }
warn() { echo -e "${YELLOW}[3Party]${NC} $*"; }
fail() { echo -e "${RED}[3Party]${NC} $*" >&2; exit 1; }

need_cmd() {
    for c in "$@"; do
        command -v "$c" &>/dev/null || fail "缺少命令: $c"
    done
}

download() {
    local url="$1" out="$2"
    if [[ -f "$out" && "$FORCE" != true ]]; then
        step "已存在缓存: $(basename "$out")"
        return 0
    fi
    step "下载: $(basename "$out")"
    curl -fL --retry 3 --connect-timeout 30 -o "$out" "$url" \
        || fail "下载失败: $url"
}

# -----------------------------------------------------------------------------
# Lua 5.4
# -----------------------------------------------------------------------------
build_lua() {
    local marker="${SCRIPT_DIR}/lua/lib/liblua.a"
    [[ -f "$marker" && "$FORCE" != true ]] && { step "Lua 已就绪，跳过"; return; }

    local tgz="${CACHE_DIR}/lua-${LUA_VERSION}.tar.gz"
    download "${LUA_URL}" "$tgz"
    rm -rf "${SRC_DIR}/lua-${LUA_VERSION}"
    tar -xzf "$tgz" -C "$SRC_DIR"
    local dir="${SRC_DIR}/lua-${LUA_VERSION}"
    [[ -d "$dir" ]] || fail "Lua 解压目录不存在"

    step "编译 Lua ${LUA_VERSION}..."
    make -C "$dir" -j"$(nproc)" linux CC=gcc MYCFLAGS="-fPIC"

    local lsrc="${dir}/src"
    mkdir -p "${SCRIPT_DIR}/lua/include" "${SCRIPT_DIR}/lua/lib"
    cp "${lsrc}"/lua.h "${lsrc}"/luaconf.h "${lsrc}"/lualib.h "${lsrc}"/lauxlib.h \
       "${SCRIPT_DIR}/lua/include/"
    cp "${lsrc}"/liblua.a "${SCRIPT_DIR}/lua/lib/"
    step "Lua → ${SCRIPT_DIR}/lua/"
}

# -----------------------------------------------------------------------------
# tinyxml2
# -----------------------------------------------------------------------------
build_tinyxml2() {
    local marker="${SCRIPT_DIR}/tinyxml2/lib/libtinyxml2.a"
    [[ -f "$marker" && "$FORCE" != true ]] && { step "tinyxml2 已就绪，跳过"; return; }

    local tgz="${CACHE_DIR}/tinyxml2-${TINYXML2_VERSION}.tar.gz"
    download "${TINYXML2_URL}" "$tgz"
    rm -rf "${SRC_DIR}/tinyxml2-${TINYXML2_VERSION}"
    tar -xzf "$tgz" -C "$SRC_DIR"
    local dir="${SRC_DIR}/tinyxml2-${TINYXML2_VERSION}"
    [[ -d "$dir" ]] || fail "tinyxml2 解压目录不存在"

    step "编译 tinyxml2 ${TINYXML2_VERSION}..."
    cmake -S "$dir" -B "${dir}/build" \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=OFF \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DCMAKE_INSTALL_PREFIX="${SCRIPT_DIR}/tinyxml2"
    cmake --build "${dir}/build" -j"$(nproc)"
    cmake --install "${dir}/build"

    mkdir -p "${SCRIPT_DIR}/tinyxml2/include" "${SCRIPT_DIR}/tinyxml2/lib"
    if [[ -f "${SCRIPT_DIR}/tinyxml2/lib64/libtinyxml2.a" ]]; then
        cp "${SCRIPT_DIR}/tinyxml2/lib64/libtinyxml2.a" "${SCRIPT_DIR}/tinyxml2/lib/"
    fi
    if [[ -f "${SCRIPT_DIR}/tinyxml2/include/tinyxml2/tinyxml2.h" ]]; then
        cp "${SCRIPT_DIR}/tinyxml2/include/tinyxml2/tinyxml2.h" \
           "${SCRIPT_DIR}/tinyxml2/include/"
    fi
    [[ -f "${SCRIPT_DIR}/tinyxml2/lib/libtinyxml2.a" ]] \
        || cp "${dir}/build/libtinyxml2.a" "${SCRIPT_DIR}/tinyxml2/lib/" 2>/dev/null || true
    [[ -f "${SCRIPT_DIR}/tinyxml2/lib/libtinyxml2.a" ]] \
        || fail "tinyxml2 静态库未生成"
    step "tinyxml2 → ${SCRIPT_DIR}/tinyxml2/"
}

# -----------------------------------------------------------------------------
# MariaDB Connector/C（兼容 mysql/mysql.h API）
# -----------------------------------------------------------------------------
build_mysql() {
    local marker="${SCRIPT_DIR}/mysql/lib/libmariadbclient.a"
    if [[ ! -f "$marker" && -f "${SCRIPT_DIR}/mysql/lib/libmariadb.a" ]]; then
        marker="${SCRIPT_DIR}/mysql/lib/libmariadb.a"
    fi
    [[ -f "$marker" && "$FORCE" != true ]] && { step "MySQL client 已就绪，跳过"; return; }

    local tgz="${CACHE_DIR}/mariadb-connector-c-${MARIADB_CONNECTOR_VERSION}-src.tar.gz"
    download "${MARIADB_CONNECTOR_URL}" "$tgz"
    rm -rf "${SRC_DIR}/mariadb-connector-c-${MARIADB_CONNECTOR_VERSION}-src"
    tar -xzf "$tgz" -C "$SRC_DIR"
    local dir="${SRC_DIR}/mariadb-connector-c-${MARIADB_CONNECTOR_VERSION}-src"
    [[ -d "$dir" ]] || fail "MariaDB Connector 解压目录不存在"

    step "编译 MariaDB Connector/C ${MARIADB_CONNECTOR_VERSION}..."
    cmake -S "$dir" -B "${dir}/build" \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=OFF \
        -DINSTALL_PLUGINDIR=lib \
        -DINSTALL_LIBDIR=lib \
        -DINSTALL_INCLUDEDIR=include \
        -DINSTALL_BINDIR=bin \
        -DINSTALL_DOCDIR=share/doc \
        -DINSTALL_MANDIR=share/man \
        -DINSTALL_TESTDIR= \
        -DINSTALL_ICONDIR=share/icons \
        -DINSTALL_LICENSEDIR=share/licenses \
        -DWITH_SSL=OPENSSL \
        -DCMAKE_INSTALL_PREFIX="${SCRIPT_DIR}/mysql"
    cmake --build "${dir}/build" -j"$(nproc)" --target mariadbclient

    mkdir -p "${SCRIPT_DIR}/mysql/include/mysql" "${SCRIPT_DIR}/mysql/lib"
    cp -r "${dir}/include/"* "${SCRIPT_DIR}/mysql/include/"
    cp "${dir}/build/include/"*.h "${SCRIPT_DIR}/mysql/include/" 2>/dev/null || true
    if [[ ! -f "${SCRIPT_DIR}/mysql/include/mysql/mysql.h" ]]; then
        cp "${dir}/include/mysql.h" "${SCRIPT_DIR}/mysql/include/mysql/mysql.h"
    fi

    local lib_src="${dir}/build/libmariadb/libmariadbclient.a"
    [[ -f "$lib_src" ]] || lib_src="$(find "${dir}/build" -name 'libmariadbclient.a' | head -1)"
    [[ -f "$lib_src" ]] || fail "libmariadbclient.a 未生成（请安装: openssl-devel zlib-devel）"
    cp "$lib_src" "${SCRIPT_DIR}/mysql/lib/libmariadbclient.a"
    ln -sf libmariadbclient.a "${SCRIPT_DIR}/mysql/lib/libmariadb.a"

    step "MySQL client → ${SCRIPT_DIR}/mysql/ (libmariadbclient.a, mysql/mysql.h)"
}

# -----------------------------------------------------------------------------
# 主流程
# -----------------------------------------------------------------------------
need_cmd curl tar make gcc g++ cmake

mkdir -p "$SRC_DIR" "$CACHE_DIR" \
    "${SCRIPT_DIR}/lua" "${SCRIPT_DIR}/tinyxml2" "${SCRIPT_DIR}/mysql"

step "===== 3Party 依赖构建 ====="
step "项目: ${PROJECT_DIR}"
step "平台: $(uname -s)-$(uname -m)"

# 编译 MariaDB 需要 OpenSSL / zlib（仅构建期，运行期静态链入）
for hdr in openssl/ssl.h zlib.h; do
    if [[ ! -f "/usr/include/${hdr}" ]]; then
        warn "未找到 /usr/include/${hdr}，若 MariaDB 编译失败请安装: openssl-devel zlib-devel"
        break
    fi
done

build_lua
build_tinyxml2
build_mysql

step "===== 3Party 构建完成 ====="
step "  lua:      ${SCRIPT_DIR}/lua/lib/liblua.a"
step "  tinyxml2: ${SCRIPT_DIR}/tinyxml2/lib/libtinyxml2.a"
step "  mysql:    ${SCRIPT_DIR}/mysql/lib/libmariadbclient.a"
step "下一步: cd ${PROJECT_DIR} && ./autoinit.sh && ./build.sh"
