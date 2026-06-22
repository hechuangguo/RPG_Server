#!/usr/bin/env bash
# =============================================================================
#  download_and_build.sh — 从 vendor 离线编译 3Party 静态库
#
#  构建产物：
#    - Lua 5.4          → 3Party/lua/{include,lib}
#    - tinyxml2         → 3Party/tinyxml2/{include,lib}
#    - MariaDB Connector/C → 3Party/mysql/{include,lib}
#
#  用法：
#    ./3Party/download_and_build.sh              # 默认：离线编译（vendor 已入库）
#    ./3Party/download_and_build.sh --build-only # 显式离线，等同默认
#    ./3Party/download_and_build.sh --fetch      # 维护者：仅下载/更新 vendor tar.gz
#    ./3Party/download_and_build.sh --force      # 强制重新下载 + 重新编译
#
#  环境依赖：
#    - tar / make / gcc / g++ / cmake（编译必需）
#    - curl（仅 --fetch / --force 且需下载时）
#    - openssl-devel / zlib-devel（MariaDB Connector 编译）
#
#  vendor 源码包见 3Party/vendor/（纳入 Git）；versions.env 定义版本与下载 URL。
# =============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
SRC_DIR="${SCRIPT_DIR}/src"
VENDOR_DIR="${SCRIPT_DIR}/vendor"

FORCE=false
FETCH_ONLY=false
BUILD_ONLY=false

for arg in "$@"; do
    case "$arg" in
        --force) FORCE=true ;;
        --fetch) FETCH_ONLY=true ;;
        --build-only) BUILD_ONLY=true ;;
        *) ;;
    esac
done

# shellcheck source=versions.env
source "${SCRIPT_DIR}/versions.env"

GREEN='\033[0;32m'; YELLOW='\033[1;33m'; RED='\033[0;31m'; NC='\033[0m'
step() { echo -e "${GREEN}[3Party]${NC} $*"; }
warn() { echo -e "${YELLOW}[3Party]${NC} $*"; }
fail() { echo -e "${RED}[3Party]${NC} $*" >&2; exit 1; }

mirror_urls() {
    local -n _out=$1
    local raw="${2:-}"
    _out=()
    if [[ -n "$raw" ]]; then
        read -r -a _out <<< "$raw"
    fi
}

need_cmd() {
    for c in "$@"; do
        command -v "$c" &>/dev/null || fail "缺少命令: $c"
    done
}

vendor_lua_tgz() { echo "${VENDOR_DIR}/lua-${LUA_VERSION}.tar.gz"; }
vendor_tinyxml2_tgz() { echo "${VENDOR_DIR}/tinyxml2-${TINYXML2_VERSION}.tar.gz"; }
vendor_mariadb_tgz() { echo "${VENDOR_DIR}/mariadb-connector-c-${MARIADB_CONNECTOR_VERSION}-src.tar.gz"; }
vendor_protobuf_tgz() { echo "${VENDOR_DIR}/${PROTOBUF_VENDOR_TGZ:-protobuf-cpp-3.${PROTOBUF_VERSION}.tar.gz}"; }

ensure_vendor_dir() {
    mkdir -p "$VENDOR_DIR"
}

# 下载单个 vendor 包（维护者路径）
download_one() {
    local out="$1"
    shift
    local urls=("$@")

    if [[ -f "$out" && "$FORCE" != true ]]; then
        step "vendor 已存在: $(basename "$out")"
        return 0
    fi
    if [[ ${#urls[@]} -eq 0 ]]; then
        fail "未配置下载 URL: $(basename "$out")"
    fi

    need_cmd curl
    local url
    for url in "${urls[@]}"; do
        [[ -n "$url" ]] || continue
        step "下载: $(basename "$out")"
        step "  尝试: $url"
        if curl -fL --retry 3 --connect-timeout 30 -o "$out" "$url"; then
            return 0
        fi
        warn "  失败，尝试下一镜像..."
        rm -f "$out"
    done

    fail "全部下载源失败: $(basename "$out")
请检查网络或手动放入 ${VENDOR_DIR}/ 后重试。"
}

fetch_lua_vendor() {
    local tgz
    tgz="$(vendor_lua_tgz)"
    download_one "$tgz" "${RPG_LUA_URL:-${LUA_URL}}"
}

fetch_tinyxml2_vendor() {
    local tgz
    tgz="$(vendor_tinyxml2_tgz)"
    local -a mirrors=()
    mirror_urls mirrors "${TINYXML2_MIRROR_URLS:-}"
    download_one "$tgz" "${RPG_TINYXML2_URL:-${TINYXML2_URL}}" "${mirrors[@]}"
}

fetch_mariadb_vendor() {
    local tgz
    tgz="$(vendor_mariadb_tgz)"
    local -a mirrors=()
    mirror_urls mirrors "${MARIADB_CONNECTOR_MIRROR_URLS:-}"
    download_one "$tgz" "${RPG_MARIADB_CONNECTOR_URL:-${MARIADB_CONNECTOR_URL}}" "${mirrors[@]}"
}

fetch_protobuf_vendor() {
    local tgz
    tgz="$(vendor_protobuf_tgz)"
    download_one "$tgz" "${RPG_PROTOBUF_URL:-${PROTOBUF_URL:-}}"
}

fetch_all_vendors() {
    ensure_vendor_dir
    step "===== 更新 vendor 源码包 ====="
    fetch_lua_vendor
    fetch_tinyxml2_vendor
    fetch_mariadb_vendor
    fetch_protobuf_vendor
    step "===== vendor 更新完成 ====="
}

# 离线模式：vendor 必须齐全，否则报错
require_vendor_or_fetch() {
    local missing=()
    [[ -f "$(vendor_lua_tgz)" ]]       || missing+=("$(basename "$(vendor_lua_tgz)")")
    [[ -f "$(vendor_tinyxml2_tgz)" ]]  || missing+=("$(basename "$(vendor_tinyxml2_tgz)")")
    [[ -f "$(vendor_mariadb_tgz)" ]]   || missing+=("$(basename "$(vendor_mariadb_tgz)")")

    if [[ ${#missing[@]} -eq 0 ]]; then
        return 0
    fi

    if [[ "$FORCE" == true ]]; then
        fetch_all_vendors
        return 0
    fi

    fail "vendor 源码包缺失: ${missing[*]}
请确认已 git clone 完整仓库（3Party/vendor/ 应含 tar.gz），
或维护者运行: ./3Party/fetch_vendor.sh"
}

prepare_vendor_for_build() {
    if [[ "$FORCE" == true ]]; then
        fetch_all_vendors
    else
        require_vendor_or_fetch
    fi
}

build_lua() {
    local marker="${SCRIPT_DIR}/lua/lib/liblua.a"
    [[ -f "$marker" && "$FORCE" != true ]] && { step "Lua 已就绪，跳过"; return; }

    local tgz
    tgz="$(vendor_lua_tgz)"
    rm -rf "${SRC_DIR}/lua-${LUA_VERSION}"
    tar --no-same-owner -xzf "$tgz" -C "$SRC_DIR"
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

build_tinyxml2() {
    local marker="${SCRIPT_DIR}/tinyxml2/lib/libtinyxml2.a"
    [[ -f "$marker" && "$FORCE" != true ]] && { step "tinyxml2 已就绪，跳过"; return; }

    local tgz
    tgz="$(vendor_tinyxml2_tgz)"
    rm -rf "${SRC_DIR}/tinyxml2-${TINYXML2_VERSION}"
    tar --no-same-owner -xzf "$tgz" -C "$SRC_DIR"
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

build_mysql() {
    local marker="${SCRIPT_DIR}/mysql/lib/libmariadbclient.a"
    if [[ ! -f "$marker" && -f "${SCRIPT_DIR}/mysql/lib/libmariadb.a" ]]; then
        marker="${SCRIPT_DIR}/mysql/lib/libmariadb.a"
    fi
    [[ -f "$marker" && "$FORCE" != true ]] && { step "MySQL client 已就绪，跳过"; return; }

    local tgz
    tgz="$(vendor_mariadb_tgz)"
    rm -rf "${SRC_DIR}/mariadb-connector-c-${MARIADB_CONNECTOR_VERSION}-src"
    tar --no-same-owner -xzf "$tgz" -C "$SRC_DIR"
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

# =============================================================================
#  主流程
# =============================================================================

mkdir -p "$VENDOR_DIR"

if [[ "$FETCH_ONLY" == true ]]; then
    fetch_all_vendors
    exit 0
fi

need_cmd tar make gcc g++ cmake
mkdir -p "$SRC_DIR" "${SCRIPT_DIR}/lua" "${SCRIPT_DIR}/tinyxml2" "${SCRIPT_DIR}/mysql"

prepare_vendor_for_build

step "===== 3Party 依赖构建（离线 vendor） ====="
step "项目: ${PROJECT_DIR}"
step "平台: $(uname -s)-$(uname -m)"

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
step "下一步: cd ${PROJECT_DIR} && ./autoinit.sh && ./Build.sh"
