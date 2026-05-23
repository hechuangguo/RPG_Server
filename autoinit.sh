#!/bin/bash
# ============================================================
#  autoinit —— 服务器环境初始化脚本
#  主要职责：
#    1. 生成/更新 common 协议头文件（可对接 protoc 等工具）
#    2. 编译第三方依赖库（lua、tinyxml2 等）
#    3. 创建必要目录和软链接
#    4. 验证配置文件存在
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
# 2. 编译 Lua 5.4（如果不存在）
# -------------------------------------------------------
LUA_LIB="$THIRD_DIR/lua/lib/liblua.a"
if [ ! -f "$LUA_LIB" ]; then
    step "Building Lua 5.4..."
    if [ ! -d "$THIRD_DIR/lua/src" ]; then
        warn "Lua source not found. Trying system lua..."
        if pkg-config --exists lua5.4 2>/dev/null; then
            step "System Lua 5.4 found, skipping build."
        else
            fail "Lua 5.4 not found. Please place lua-5.4.x source in $THIRD_DIR/lua/"
        fi
    else
        cd "$THIRD_DIR/lua"
        make -j$(nproc) linux
        mkdir -p lib include
        cp lua.h lualib.h lauxlib.h luaconf.h include/
        cp liblua.a lib/
        cd "$SCRIPT_DIR"
        step "Lua built successfully."
    fi
fi

# -------------------------------------------------------
# 3. 编译 tinyxml2（如果不存在）
# -------------------------------------------------------
TINYXML2_LIB="$THIRD_DIR/tinyxml2/lib/libtinyxml2.a"
if [ ! -f "$TINYXML2_LIB" ]; then
    step "Building tinyxml2..."
    if [ ! -d "$THIRD_DIR/tinyxml2/src" ]; then
        warn "tinyxml2 source not found. Trying system libtinyxml2..."
        if ldconfig -p | grep -q tinyxml2; then
            step "System tinyxml2 found, skipping build."
        else
            warn "tinyxml2 not found. CMake will use system package if available."
        fi
    else
        cd "$THIRD_DIR/tinyxml2"
        mkdir -p build && cd build
        cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
              -DCMAKE_INSTALL_PREFIX="$THIRD_DIR/tinyxml2"
        make -j$(nproc) && make install
        cd "$SCRIPT_DIR"
        step "tinyxml2 built successfully."
    fi
fi

# -------------------------------------------------------
# 4. 检查 MySQL client 库
# -------------------------------------------------------
if ! ldconfig -p | grep -q libmysqlclient; then
    warn "libmysqlclient not found. Install: sudo apt install libmysqlclient-dev"
fi

# -------------------------------------------------------
# 5. 检查配置文件
# -------------------------------------------------------
step "Checking config files..."
[ -f "$SCRIPT_DIR/config/config.xml"     ] || fail "Missing config/config.xml"
[ -f "$SCRIPT_DIR/config/server_info.xml"] || fail "Missing config/server_info.xml"
step "Config files OK."

# -------------------------------------------------------
# 6. 更新协议（预留：对接 protoc 或自定义代码生成）
# -------------------------------------------------------
step "Updating protocol files..."
# 如果使用 protobuf：
# protoc --cpp_out=./common ./common/*.proto
step "Protocol files OK (using header-only structs)."

# -------------------------------------------------------
# 7. 设置脚本执行权限
# -------------------------------------------------------
chmod +x "$SCRIPT_DIR/RunServer.sh"
chmod +x "$SCRIPT_DIR/StopServer.sh"
chmod +x "$SCRIPT_DIR/log.sh"

# -------------------------------------------------------
# 8. CMake 配置（不编译，仅生成 Makefile）
# -------------------------------------------------------
step "Configuring CMake..."
cd "$BUILD_DIR"
cmake "$SCRIPT_DIR" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cd "$SCRIPT_DIR"

step "===== AutoInit complete ====="
step "Next steps:"
step "  1. cd build && make -j\$(nproc)   # 编译所有服务器"
step "  2. cd ..   && ./RunServer.sh      # 启动所有服务器"
step "  3. ./log.sh                       # 实时查看日志"
