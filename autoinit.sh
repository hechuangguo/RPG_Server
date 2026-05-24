#!/bin/bash
# ============================================================
#  autoinit —— 服务器环境初始化脚本
#  主要职责：
#    1. 构建 3Party 第三方依赖（Lua / tinyxml2 / MySQL client）
#    2. 创建必要目录
#    3. 验证配置文件存在
#    4. CMake configure
# ============================================================

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
THIRD_DIR="$SCRIPT_DIR/3Party"
BUILD_DIR="$SCRIPT_DIR/build"
LOG_DIR="$SCRIPT_DIR/logs"
RUN_DIR="$SCRIPT_DIR/run"

GREEN="\033[0;32m"
YELLOW="\033[1;33m"
RED="\033[0;31m"
NC="\033[0m"

step() { echo -e "${GREEN}[INIT]${NC} $1"; }
warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
fail() { echo -e "${RED}[FAIL]${NC} $1"; exit 1; }

step "===== RPG Server AutoInit ====="

# -------------------------------------------------------
# 1. 创建必要目录
# -------------------------------------------------------
step "Creating directories..."
mkdir -p "$LOG_DIR" "$RUN_DIR" "$BUILD_DIR" "$THIRD_DIR"

# -------------------------------------------------------
# 2. 构建 3Party 依赖（Lua / tinyxml2 / MySQL client）
# -------------------------------------------------------
LUA_LIB="$THIRD_DIR/lua/lib/liblua.a"
TINYXML2_LIB="$THIRD_DIR/tinyxml2/lib/libtinyxml2.a"
MYSQL_LIB="$THIRD_DIR/mysql/lib/libmariadbclient.a"
if [[ ! -f "$MYSQL_LIB" ]]; then
    MYSQL_LIB="$THIRD_DIR/mysql/lib/libmariadb.a"
fi

if [[ ! -f "$LUA_LIB" || ! -f "$TINYXML2_LIB" || ! -f "$MYSQL_LIB" ]]; then
    step "Building 3Party dependencies..."
    if [[ ! -x "$THIRD_DIR/download_and_build.sh" ]]; then
        chmod +x "$THIRD_DIR/download_and_build.sh"
    fi
    "$THIRD_DIR/download_and_build.sh" \
        || fail "3Party build failed. See 3Party/README.md"
else
    step "3Party libraries already built."
fi

# -------------------------------------------------------
# 3. 检查配置文件
# -------------------------------------------------------
step "Checking config files..."
[ -f "$SCRIPT_DIR/config/config.xml"     ] || fail "Missing config/config.xml"
[ -f "$SCRIPT_DIR/config/server_info.xml" ] || fail "Missing config/server_info.xml"
step "Config files OK."

# -------------------------------------------------------
# 4. 更新协议（预留）
# -------------------------------------------------------
step "Protocol files OK (using header-only structs)."

# -------------------------------------------------------
# 5. 设置脚本执行权限
# -------------------------------------------------------
chmod +x "$SCRIPT_DIR/RunServer.sh"
chmod +x "$SCRIPT_DIR/StopServer.sh"
chmod +x "$SCRIPT_DIR/log.sh"
chmod +x "$SCRIPT_DIR/build.sh" 2>/dev/null || true

# -------------------------------------------------------
# 6. CMake 配置
# -------------------------------------------------------
step "Configuring CMake..."
cd "$BUILD_DIR"
cmake "$SCRIPT_DIR" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cd "$SCRIPT_DIR"

step "===== AutoInit complete ====="
step "Next steps:"
step "  1. ./build.sh                    # 编译所有服务器"
step "  2. ./RunServer.sh                # 启动所有服务器"
step "  3. ./log.sh                      # 实时查看日志"
