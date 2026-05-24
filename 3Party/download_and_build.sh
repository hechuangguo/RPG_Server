#!/usr/bin/env bash
# =============================================================================
#  download_and_build.sh — 下载并编译 3Party 依赖到本目录
#
#  下载/构建的第三方库：
#    - Lua 5.4          → 3Party/lua/{include,lib}       （脚本引擎）
#    - tinyxml2         → 3Party/tinyxml2/{include,lib}   （XML 解析器）
#    - MariaDB Connector/C (MySQL 客户端 API)
#                        → 3Party/mysql/{include,lib}     （数据库连接）
#
#  用法：
#    ./3Party/download_and_build.sh          # 增量构建（已有产物则跳过）
#    ./3Party/download_and_build.sh --force  # 强制重新下载并编译所有库
#
#  环境依赖（预置条件）：
#    - curl：下载源码包
#    - tar：解压 .tar.gz 压缩包
#    - make：编译 Lua（makefile 项目）
#    - gcc / g++：C/C++ 编译器
#    - cmake：构建 tinyxml2 和 MariaDB Connector
#    - OpenSSL / zlib 开发头文件：MariaDB Connector 编译依赖
#
#  版本管理：
#    各库的版本号和下载 URL 定义在 versions.env 文件中，
#    修改该文件即可切换库版本，无需修改本脚本逻辑
# =============================================================================

# -------------------------------------------------------
#  严格模式说明
#  - set -e：任何命令返回非零状态码立即退出
#  - set -u：使用未定义变量时报错退出（防止拼写错误）
#  - set -o pipefail：管线中任一命令失败，整个管线返回失败
#    例：false | true 在默认 bash 下会成功，pipefail 下会因 false 而失败
# -------------------------------------------------------
set -euo pipefail

# 路径定义
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"  # 3Party/ 目录
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"               # 项目根目录
SRC_DIR="${SCRIPT_DIR}/src"      # 源码解压和编译工作目录
CACHE_DIR="${SCRIPT_DIR}/cache"  # 下载缓存目录（.tar.gz 存档）

# 是否强制重新构建（--force 参数）
FORCE=false
[[ "${1:-}" == "--force" ]] && FORCE=true

# 加载版本信息：LUA_VERSION、LUA_URL、TINYXML2_VERSION、TINYXML2_URL、
# MARIADB_CONNECTOR_VERSION、MARIADB_CONNECTOR_URL 等变量在此定义
# shellcheck source=versions.env
source "${SCRIPT_DIR}/versions.env"

# 终端颜色和日志函数
GREEN='\033[0;32m'; YELLOW='\033[1;33m'; RED='\033[0;31m'; NC='\033[0m'
step() { echo -e "${GREEN}[3Party]${NC} $*"; }
warn() { echo -e "${YELLOW}[3Party]${NC} $*"; }
fail() { echo -e "${RED}[3Party]${NC} $*" >&2; exit 1; }

# -------------------------------------------------------
#  need_cmd：检查系统是否安装了必要命令
#  参数：可变的命令名称列表
#  用法：need_cmd curl tar make gcc g++ cmake
#  在构建开始前统一检查，避免编译到半途因缺少工具而失败
# -------------------------------------------------------
need_cmd() {
    for c in "$@"; do
        command -v "$c" &>/dev/null || fail "缺少命令: $c"
    done
}

# -------------------------------------------------------
#  download：下载文件并缓存
#  参数：$1=下载URL  $2=本地输出路径
#
#  下载策略：
#    - 首先检查本地缓存文件是否已存在且非 --force 模式
#    - 缓存命中则跳过下载，节省带宽和时间（增量构建）
#    - 下载失败重试最多 3 次（--retry 3）
#    - 连接超时设为 30 秒（--connect-timeout 30），避免卡死
#    - 使用 -L 跟随 HTTP 重定向
#    - 下载失败调用 fail() 退出，不继续编译不完整源码
# -------------------------------------------------------
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
#  构建 Lua 5.4
#
#  编译选项说明：
#    • linux：目标平台为 Linux（排除 Windows/macOS 特定代码）
#    • CC=gcc：指定使用 GCC 编译器
#    • MYCFLAGS="-fPIC"：生成位置无关代码（Position Independent Code）
#      - -fPIC 是编译为静态库时链接到共享库的前置条件
#      - 确保生成的 .o 文件可以被任意地址加载
#      - 对后续链接到项目服务器二进制是必需的
#    • -j$(nproc)：并行编译，使用全部 CPU 核心加速
#
#  构建产物：
#    • 3Party/lua/include/：lua.h、luaconf.h、lualib.h、lauxlib.h
#    • 3Party/lua/lib/liblua.a：Lua 静态库
# -----------------------------------------------------------------------------
build_lua() {
    local marker="${SCRIPT_DIR}/lua/lib/liblua.a"
    # 增量构建检查：静态库已存在且非 --force 模式则跳过
    [[ -f "$marker" && "$FORCE" != true ]] && { step "Lua 已就绪，跳过"; return; }

    local tgz="${CACHE_DIR}/lua-${LUA_VERSION}.tar.gz"
    download "${LUA_URL}" "$tgz"

    # 清理旧源码目录，确保干净的编译环境
    rm -rf "${SRC_DIR}/lua-${LUA_VERSION}"
    tar -xzf "$tgz" -C "$SRC_DIR"
    local dir="${SRC_DIR}/lua-${LUA_VERSION}"
    [[ -d "$dir" ]] || fail "Lua 解压目录不存在"

    step "编译 Lua ${LUA_VERSION}..."
    make -C "$dir" -j"$(nproc)" linux CC=gcc MYCFLAGS="-fPIC"

    # 安装头文件和静态库到 3Party/lua/ 目录
    local lsrc="${dir}/src"
    mkdir -p "${SCRIPT_DIR}/lua/include" "${SCRIPT_DIR}/lua/lib"
    cp "${lsrc}"/lua.h "${lsrc}"/luaconf.h "${lsrc}"/lualib.h "${lsrc}"/lauxlib.h \
       "${SCRIPT_DIR}/lua/include/"
    cp "${lsrc}"/liblua.a "${SCRIPT_DIR}/lua/lib/"
    step "Lua → ${SCRIPT_DIR}/lua/"
}

# -----------------------------------------------------------------------------
#  构建 tinyxml2
#
#  CMake 编译选项说明：
#    • CMAKE_BUILD_TYPE=Release：开启优化，移除调试信息
#    • BUILD_SHARED_LIBS=OFF：编译为静态库（.a），不做动态链接（.so/.dll）
#      - 静态库随可执行文件一起分发，无运行时依赖问题
#    • CMAKE_POSITION_INDEPENDENT_CODE=ON：等价于 CFLAGS 加 -fPIC
#      - 确保 .o 文件使用位置无关代码，链接到主项目时不出错
#    • CMAKE_INSTALL_PREFIX：安装目标路径（install 时复制产物到此）
#
#  install 后处理：
#    由于不同 CMake 版本/lib 路径约定差异，库可能安装在 lib64/
#    或直接编译输出在 build/ 目录，需做兼容处理：
#    1. 优先从 lib64/ 复制（某些 Linux 发行版的默认行为）
#    2. 从 include/tinyxml2/ 子目录取头文件
#    3. 回退到 build/ 目录取编译产物
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
    # cmake -S：指定源码目录，-B：指定构建目录
    cmake -S "$dir" -B "${dir}/build" \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=OFF \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DCMAKE_INSTALL_PREFIX="${SCRIPT_DIR}/tinyxml2"
    cmake --build "${dir}/build" -j"$(nproc)"
    cmake --install "${dir}/build"

    # 整理产物到标准位置
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
#  构建 MariaDB Connector/C（兼容 mysql/mysql.h API）
#
#  CMake 编译选项说明：
#    • BUILD_SHARED_LIBS=OFF：编译为静态库（生产部署友好，无需 .so 依赖）
#    • WITH_SSL=OPENSSL：使用系统 OpenSSL 提供 TLS/SSL 加密连接
#      - 需要事先安装 openssl-devel（CentOS）或 libssl-dev（Ubuntu）
#    • INSTALL_PLUGINDIR/LIBDIR/INCLUDEDIR/BINDIR：安装路径细分
#      - 将不同组件的安装路径精确控制到自定义目录
#    • --target mariadbclient：仅编译客户端库，跳过服务端和测试
#      - 大幅减少编译时间和依赖
#
#  兼容说明：
#    - 库文件名在旧版本为 libmariadbclient.a，新版本改为 libmariadb.a
#    - 本脚本生成两份（硬链接），保证两种命名都能被 CMake 找到
#    - 头文件路径兼容 mysql/mysql.h 的传统 #include 写法
# -----------------------------------------------------------------------------
build_mysql() {
    # 兼容新旧版本的库文件命名差异
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

    # 安装头文件：复制所有 include/ 内容
    mkdir -p "${SCRIPT_DIR}/mysql/include/mysql" "${SCRIPT_DIR}/mysql/lib"
    cp -r "${dir}/include/"* "${SCRIPT_DIR}/mysql/include/"
    # cmake 构建过程中生成的头文件（如 mariadb_version.h）也需复制
    cp "${dir}/build/include/"*.h "${SCRIPT_DIR}/mysql/include/" 2>/dev/null || true
    # 确保 mysql/mysql.h 存在于标准路径（兼容项目中的 #include <mysql/mysql.h>）
    if [[ ! -f "${SCRIPT_DIR}/mysql/include/mysql/mysql.h" ]]; then
        cp "${dir}/include/mysql.h" "${SCRIPT_DIR}/mysql/include/mysql/mysql.h"
    fi

    # 查找并复制静态库
    # CMake 不同版本下产物路径可能不同：
    #   标准：libmariadb/libmariadbclient.a
    #   备选：libmariadb.a（新版本重命名）
    local lib_src="${dir}/build/libmariadb/libmariadbclient.a"
    [[ -f "$lib_src" ]] || lib_src="$(find "${dir}/build" -name 'libmariadbclient.a' | head -1)"
    [[ -f "$lib_src" ]] || fail "libmariadbclient.a 未生成（请安装: openssl-devel zlib-devel）"
    cp "$lib_src" "${SCRIPT_DIR}/mysql/lib/libmariadbclient.a"
    # 创建 libmariadb.a 的符号链接（兼容新旧命名约定）
    ln -sf libmariadbclient.a "${SCRIPT_DIR}/mysql/lib/libmariadb.a"

    step "MySQL client → ${SCRIPT_DIR}/mysql/ (libmariadbclient.a, mysql/mysql.h)"
}

# =============================================================================
#  主流程
# =============================================================================

# 1. 预检查：验证所有必需的系统工具是否可用
#   提前失败，避免在缺少工具时下载了源码却无法编译
need_cmd curl tar make gcc g++ cmake

# 2. 创建工作目录（src: 源码编译区，cache: 下载缓存区）
#    库输出目录也预先创建，确保后续 cp 操作路径存在
mkdir -p "$SRC_DIR" "$CACHE_DIR" \
    "${SCRIPT_DIR}/lua" "${SCRIPT_DIR}/tinyxml2" "${SCRIPT_DIR}/mysql"

step "===== 3Party 依赖构建 ====="
step "项目: ${PROJECT_DIR}"
step "平台: $(uname -s)-$(uname -m)"

# 3. 检查 MariaDB 编译所需的系统头文件
#    OpenSSL：提供 TLS/SSL 加密连接能力
#    zlib：提供数据压缩传输能力
#    仅编译时需要（运行期静态链入主程序），缺失时仅警告不阻断
for hdr in openssl/ssl.h zlib.h; do
    if [[ ! -f "/usr/include/${hdr}" ]]; then
        warn "未找到 /usr/include/${hdr}，若 MariaDB 编译失败请安装: openssl-devel zlib-devel"
        break
    fi
done

# 4. 按顺序编译三个库（彼此独立，无先后依赖）
build_lua
build_tinyxml2
build_mysql

step "===== 3Party 构建完成 ====="
step "  lua:      ${SCRIPT_DIR}/lua/lib/liblua.a"
step "  tinyxml2: ${SCRIPT_DIR}/tinyxml2/lib/libtinyxml2.a"
step "  mysql:    ${SCRIPT_DIR}/mysql/lib/libmariadbclient.a"
step "下一步: cd ${PROJECT_DIR} && ./autoinit.sh && ./build.sh"
