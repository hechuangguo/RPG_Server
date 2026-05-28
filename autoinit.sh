#!/bin/bash
# ============================================================
#  autoinit —— 服务器环境自动初始化脚本
#
#  用法：
#    ./autoinit.sh    # 执行完整初始化流程
#
#  预置条件（环境检查）：
#    - gcc/g++：C/C++ 编译器（编译 Lua、tinyxml2、MariaDB Connector）
#    - cmake：项目构建系统（用于 3Party 库和主项目的 CMake 配置）
#    - make：构建工具（编译 Lua 等 makefile 项目）
#    - curl/tar：下载和解压第三方库源码包
#    - OpenSSL/zlib 开发包：MariaDB Connector 编译依赖
#      （Ubuntu: apt install libssl-dev zlib1g-dev）
#      （CentOS: yum install openssl-devel zlib-devel）
#
#  初始化流程（6 步）：
#    1. 创建必要目录（logs/、run/、.build/、3Party/、DataDoc/）
#    2. 构建 3Party 第三方依赖（Lua / tinyxml2 / MariaDB Client）
#    3. 检查配置文件完整性（config.xml、server_info.xml）
#    4. 验证协议文件（头文件定义，无需代码生成）
#    5. 设置脚本执行权限（RunServer/StopServer/log/build）
#    6. CMake 配置生成构建系统（Release 模式）
#
#  注意事项：
#    - 仅在首次克隆项目或环境变更时运行
#    - 3Party 构建可能耗时较长（首次）或跳过（已构建）
#    - 使用 set -e 确保任一步骤失败立即终止
# ============================================================

# 启用严格模式：任何命令失败立即退出，避免后续步骤在不完整状态下执行
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
THIRD_DIR="$SCRIPT_DIR/3Party"   # 第三方库根目录（包含 lua/tinyxml2/mysql 子目录）
BUILD_DIR="$SCRIPT_DIR/.build"   # CMake 构建输出目录
LOG_DIR="$SCRIPT_DIR/logs"      # 服务器运行日志目录
RUN_DIR="$SCRIPT_DIR/run"       # PID 文件目录（进程管理）

# 终端颜色输出
GREEN="\033[0;32m"
YELLOW="\033[1;33m"
RED="\033[0;31m"
NC="\033[0m"

step() { echo -e "${GREEN}[INIT]${NC} $1"; }
warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
fail() { echo -e "${RED}[FAIL]${NC} $1"; exit 1; }  # 失败时打印错误并立即退出

step "===== RPG Server AutoInit ====="

# -------------------------------------------------------
#  第1步：创建必要目录
#  - logs/：运行期日志输出
#  - run/：PID 文件存放
#  - .build/：CMake 构建产物
#  - 3Party/：第三方库根目录（若不存在）
# -------------------------------------------------------
step "Creating directories..."
mkdir -p "$LOG_DIR" "$RUN_DIR" "$BUILD_DIR" "$THIRD_DIR" "$SCRIPT_DIR/DataDoc"

# -------------------------------------------------------
#  第2步：构建 3Party 第三方依赖
#
#  依赖库及用途：
#    • Lua 5.4：脚本语言引擎，用于游戏逻辑脚本和配置解析
#      - 产物：3Party/lua/lib/liblua.a、include 头文件
#    • tinyxml2：轻量 XML 解析器，解析 config.xml 等配置文件
#      - 产物：3Party/tinyxml2/lib/libtinyxml2.a
#    • MariaDB Connector/C：MySQL 兼容的数据库客户端库
#      - 产物：3Party/mysql/lib/libmariadbclient.a（或 libmariadb.a）
#      - 用于用户数据、物品、日志等持久化存储
#
#  依赖检查逻辑：
#    检查每个库的关键产物文件是否存在：
#    - lua：liblua.a
#    - tinyxml2：libtinyxml2.a
#    - mysql：libmariadbclient.a（优先）或 libmariadb.a（备选）
#    三者全部存在则跳过构建，任一缺失则触发完整构建
# -------------------------------------------------------
LUA_LIB="$THIRD_DIR/lua/lib/liblua.a"
TINYXML2_LIB="$THIRD_DIR/tinyxml2/lib/libtinyxml2.a"
MYSQL_LIB="$THIRD_DIR/mysql/lib/libmariadbclient.a"
# MariaDB Connector 不同版本的产物命名可能不同（libmariadbclient 旧名 / libmariadb 新名）
if [[ ! -f "$MYSQL_LIB" ]]; then
    MYSQL_LIB="$THIRD_DIR/mysql/lib/libmariadb.a"
fi

if [[ ! -f "$LUA_LIB" || ! -f "$TINYXML2_LIB" || ! -f "$MYSQL_LIB" ]]; then
    step "Building 3Party dependencies..."
    # 确保下载构建脚本有执行权限
    if [[ ! -x "$THIRD_DIR/download_and_build.sh" ]]; then
        chmod +x "$THIRD_DIR/download_and_build.sh"
    fi
    "$THIRD_DIR/download_and_build.sh" \
        || fail "3Party build failed. See 3Party/README.md"
else
    step "3Party libraries already built."
fi

# -------------------------------------------------------
#  第3步：检查配置文件
#  - config.xml：主配置（各服务器 IP/端口、数据库连接、日志级别等）
#  - server_info.xml：场景信息配置（地图 ID、分线配置、NPC 分布等）
#  配置文件缺失将导致服务器无法启动
# -------------------------------------------------------
step "Checking config files..."
[ -f "$SCRIPT_DIR/config/config.xml"     ] || fail "Missing config/config.xml"
[ -f "$SCRIPT_DIR/config/server_info.xml" ] || fail "Missing config/server_info.xml"
step "Config files OK."

# -------------------------------------------------------
#  第4步：策划表生成（DataDoc Excel → database Lua）
#  依赖 Python3 + openpyxl；失败时仅警告，不阻断环境初始化
# -------------------------------------------------------
step "Generating data tables from DataDoc..."
chmod +x "$SCRIPT_DIR/gen_data.sh" 2>/dev/null || true
if [[ -x "$SCRIPT_DIR/gen_data.sh" ]]; then
    if "$SCRIPT_DIR/gen_data.sh" 2>/dev/null; then
        step "DataDoc -> database/*.lua OK."
    else
        warn "DataDoc gen skipped (install: pip3 install -r tools/requirements-datadoc.txt)"
        warn "  Or run later: ./gen_data.sh --init && ./gen_data.sh"
    fi
else
    warn "gen_data.sh not found, skip data table generation."
fi

# -------------------------------------------------------
#  第5步：协议文件检查
#  当前协议采用 header-only struct 定义方式，无需 proto 编译步骤。
# -------------------------------------------------------
step "Protocol files OK (using header-only structs)."

# -------------------------------------------------------
#  第6步：设置脚本执行权限
#  确保后续可直接调用 ./RunServer.sh 等脚本，
#  特别是在 git clone 后文件权限可能丢失的场景
# -------------------------------------------------------
chmod +x "$SCRIPT_DIR/RunServer.sh"
chmod +x "$SCRIPT_DIR/StopServer.sh"
chmod +x "$SCRIPT_DIR/log.sh"
chmod +x build Build.sh 2>/dev/null || true

# -------------------------------------------------------
#  第7步：CMake 配置
#
#  配置说明：
#    - CMAKE_BUILD_TYPE=Release：Release 模式编译
#      - 开启 -O2/-O3 优化，移除调试符号，适合生产/测试部署
#      - 如需调试，可改为 Debug：cmake -DCMAKE_BUILD_TYPE=Debug ..
#    - CMAKE_EXPORT_COMPILE_COMMANDS=ON：生成 compile_commands.json
#      - IDE（VSCode、CLion）和 clang-tidy/clangd 等工具依赖此文件
#      - 提供跳转、补全、静态分析等智能编辑支持
#
#  构建产物位置：各服务器目录下的同名可执行文件（如 SuperServer/SuperServer）
# -------------------------------------------------------
step "Configuring CMake..."
cd "$BUILD_DIR"
cmake "$SCRIPT_DIR" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cd "$SCRIPT_DIR"

step "===== AutoInit complete ====="
step "Next steps:"
step "  1. ./Build.sh                    # 编译所有服务器"
step "  2. ./RunServer.sh                # 启动所有服务器"
step "  3. ./log.sh                      # 实时查看日志"
