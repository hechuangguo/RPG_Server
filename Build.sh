#!/usr/bin/env bash
# =============================================================================
#  build.sh  —  RPG Server 统一编译脚本
#
#  功能：提供一键式构建入口，封装 cmake 配置 + make 编译的完整流程
#        支持增量编译、指定目标编译、多并发编译、Debug/Release 切换等
#
#  用法：
#    ./build.sh               # 默认 Release 编译，并发数 = CPU 核数
#    ./build.sh -j10          # 指定 10 并发编译
#    ./build.sh debug         # Debug 模式（不优化，ASAN 开启）
#    ./build.sh clean         # 清除构建目录
#    ./build.sh rebuild       # 先 clean 再全量编译
#    ./build.sh SuperServer   # 只编译指定服务器（支持多个，空格分隔）
#    ./build.sh -j8 debug     # 8 并发 Debug 模式
#    ./build.sh -j8 SceneServer RecordServer  # 8 并发编译指定服务器
#
#  参数说明：
#    -j<N>              make 并发编译线程数，默认自动检测 CPU 核数（nproc/hw.ncpu）
#    debug              编译类型切换为 Debug（无优化，带调试符号，可启用 ASAN）
#    release            编译类型切换为 Release（最高优化 -O3，不含调试符号）
#    clean              删除 .build/ 构建目录，清除所有编译产物和 CMake 缓存
#    rebuild            等价于 clean + 全量重新编译（用于解决依赖更新导致的编译问题）
#    <ServerName>...    只构建指定的服务器目标（空格分隔多个名称），跳过其他目标
#
#  输出：
#    编译产物输出到 .build/bin/ 目录
#    日志输出到 .build/cmake_configure.log 和 .build/build.log
# =============================================================================

set -euo pipefail

# ──────────────────────────────────────────────
# ANSI 颜色定义 — 用于终端彩色输出
# 红色=错误  绿色=成功  黄色=警告  青色=信息  粗体=标题
# ──────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; BOLD='\033[1m'; RESET='\033[0m'

info()    { echo -e "${CYAN}[INFO]${RESET}  $*"; }        # 信息日志函数
success() { echo -e "${GREEN}[OK]${RESET}    $*"; }         # 成功提示函数
warn()    { echo -e "${YELLOW}[WARN]${RESET}  $*"; }        # 警告提示函数
error()   { echo -e "${RED}[ERR]${RESET}   $*" >&2; exit 1; } # 错误处理函数（输出到stderr并退出）

# ──────────────────────────────────────────────
# 路径基准变量
# SCRIPT_DIR: 脚本所在目录（即项目根目录）
# BUILD_DIR : CMake 构建输出目录（所有中间文件和产物在此生成）
# BIN_DIR   : 最终可执行文件输出目录（BUILD_DIR 的子目录）
# ──────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/.build"
BIN_DIR="${BUILD_DIR}/bin"

# ──────────────────────────────────────────────
# 所有合法的服务器名称列表（用于参数校验和全量编译遍历）
# ──────────────────────────────────────────────
ALL_SERVERS=(
    SuperServer SessionServer RecordServer AOIServer
    SceneServer GatewayServer LoggerServer GlobalServer ZoneServer
)

# ──────────────────────────────────────────────
# 默认参数值
# JOBS      : 并行编译线程数，默认根据CPU核数自动设置
# BUILD_TYPE: CMake 编译类型（RelWithDebInfo = Release优化 + 调试符号）
# DO_CLEAN   : 是否执行清理操作
# DO_REBUILD: 是否执行 clean 后重建
# TARGETS   : 指定编译的目标列表（空数组表示全部编译）
# ──────────────────────────────────────────────
JOBS=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
BUILD_TYPE="RelWithDebInfo"      # Release 带调试符号（推荐生产使用，兼顾性能和调试能力）
DO_CLEAN=false
DO_REBUILD=false
TARGETS=()                       # 空表示构建全部目标

# ──────────────────────────────────────────────
# 命令行参数解析函数
# 支持的参数格式：-j<N> / debug / release / clean / rebuild / <ServerName>
# 未知参数会输出警告并忽略，不会中断脚本执行
# ──────────────────────────────────────────────
parse_args() {
    for arg in "$@"; do
        case "${arg}" in
            -j[0-9]*)
                # 解析 -j 参数后的数字作为并行线程数
                JOBS="${arg#-j}"
                ;;
            debug|Debug|DEBUG)
                # Debug 模式：关闭优化，启用调试信息，方便断点调试
                BUILD_TYPE="Debug"
                ;;
            release|Release|RELEASE)
                # Release 模式：最高优化级别，不包含调试符号，用于性能测试和生产部署
                BUILD_TYPE="Release"
                ;;
            clean|Clean|CLEAN)
                # 清理标志：删除整个 build 目录后退出
                DO_CLEAN=true
                ;;
            rebuild|Rebuild|REBUILD)
                # 重建标志：先清理再全量编译
                DO_REBUILD=true
                ;;
            *)
                # 尝试匹配为合法服务器名称
                local valid=false
                for s in "${ALL_SERVERS[@]}"; do
                    if [[ "${arg}" == "${s}" ]]; then
                        valid=true
                        TARGETS+=("${arg}")
                        break
                    fi
                done
                if [[ "${valid}" == false ]]; then
                    warn "未知参数：${arg}，已忽略"
                fi
                ;;
        esac
    done
}

# ──────────────────────────────────────────────
# 工具链依赖检查
# 验证必要的编译工具是否已安装（cmake/make/g++/curl）
# 若缺失则给出各发行版的安装命令提示并终止
# 同时检查 3Party 第三方库是否已预编译，未编译则自动触发 autoinit
# ──────────────────────────────────────────────
check_deps() {
    local missing=()
    # 检查必要工具是否存在
    for tool in cmake make g++ curl; do
        if ! command -v "${tool}" &>/dev/null; then
            missing+=("${tool}")
        fi
    done
    if [[ ${#missing[@]} -gt 0 ]]; then
        error "缺少必要工具：${missing[*]}
  CentOS/RHEL: sudo dnf install -y gcc-c++ cmake make curl tar openssl-devel zlib-devel
  Ubuntu/Debian: sudo apt install -y build-essential cmake curl libssl-dev zlib1g-dev"
    fi

    # 检查第三方依赖库是否已预编译（以 Lua 库为检测基准）
    local lua_lib="${SCRIPT_DIR}/3Party/lua/lib/liblua.a"
  if [[ ! -f "${lua_lib}" ]]; then
        warn "3Party 依赖未构建，正在自动执行 autoinit..."
        "${SCRIPT_DIR}/autoinit.sh"
    fi
}

# ──────────────────────────────────────────────
# 打印构建环境信息摘要
# 显示项目路径、编译配置、目标列表等信息供用户确认
# ──────────────────────────────────────────────
print_env() {
    echo -e "${BOLD}=====================================================${RESET}"
    echo -e "${BOLD}  RPG Server Build Script${RESET}"
    echo -e "${BOLD}=====================================================${RESET}"
    info "项目根目录  : ${SCRIPT_DIR}"
    info "构建目录    : ${BUILD_DIR}"
    info "输出目录    : ${BIN_DIR}"
    info "编译类型    : ${BUILD_TYPE}"
    info "并发数      : ${JOBS}"
    if [[ ${#TARGETS[@]} -gt 0 ]]; then
        info "编译目标    : ${TARGETS[*]}"
    else
        info "编译目标    : 全部 (${ALL_SERVERS[*]})"
    fi
    echo ""
}

# ──────────────────────────────────────────────
# 清理构建目录
# 删除整个 build/ 目录及其内容（含 CMake 缓存、中间 .o 文件、可执行文件等）
# ──────────────────────────────────────────────
do_clean() {
    if [[ -d "${BUILD_DIR}" ]]; then
        info "清除构建目录：${BUILD_DIR}"
        rm -rf "${BUILD_DIR}"
        success "清除完成"
    else
        warn "构建目录不存在，无需清除"
    fi
}

# ──────────────────────────────────────────────
# CMake 配置阶段
# 在 build/ 目录中运行 CMake 配置，生成 Makefile 或 Ninja 构建文件
# 输出同时打印到终端和日志文件（cmake_configure.log）便于排错
# 参数说明：
#   -S : 源码根目录路径
#   -B : 构建输出目录路径
#   -DCMAKE_BUILD_TYPE : 编译类型（Debug/Release/RelWithDebInfo）
#   -DCMAKE_EXPORT_COMPILE_COMMANDS=ON : 生成 compile_commands.json（供 LSP/IDE 使用）
# ──────────────────────────────────────────────
do_configure() {
    info "运行 cmake 配置..."
    mkdir -p "${BUILD_DIR}"
    cmake \
        -S "${SCRIPT_DIR}" \
        -B "${BUILD_DIR}" \
        -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        2>&1 | tee "${BUILD_DIR}/cmake_configure.log"

    if [[ ${PIPESTATUS[0]} -ne 0 ]]; then
        error "cmake 配置失败！详见：${BUILD_DIR}/cmake_configure.log"
    fi
    success "cmake 配置完成"
}

# ──────────────────────────────────────────────
# Make 编译阶段
# 使用 cmake --build 调用底层构建系统（make/ninja）进行实际编译
# 支持指定目标编译或全量编译，输出同时记录到 build.log
# 编译完成后统计耗时并显示结果
# ──────────────────────────────────────────────
do_build() {
    local start_ts end_ts elapsed

    # 根据 TARGETS 数组决定是全量编译还是指定目标编译
    local make_targets=()
    if [[ ${#TARGETS[@]} -gt 0 ]]; then
        make_targets=("${TARGETS[@]}")
    fi
    # 空 make_targets 表示 make all（全量编译）

    info "开始编译 (make -j${JOBS})..."
    start_ts=$(date +%s)

    if [[ ${#make_targets[@]} -gt 0 ]]; then
        # 指定目标模式：只编译 TARGETS 中列出的服务器
        cmake --build "${BUILD_DIR}" \
              --parallel "${JOBS}" \
              --target "${make_targets[@]}" \
              2>&1 | tee "${BUILD_DIR}/build.log"
    else
        # 全量编译模式：编译项目中定义的所有目标
        cmake --build "${BUILD_DIR}" \
              --parallel "${JOBS}" \
              2>&1 | tee "${BUILD_DIR}/build.log"
    fi

    if [[ ${PIPESTATUS[0]} -ne 0 ]]; then
        error "编译失败！详见：${BUILD_DIR}/build.log"
    fi

    end_ts=$(date +%s)
    elapsed=$(( end_ts - start_ts ))
    success "编译完成，耗时 ${elapsed} 秒"
}

# ──────────────────────────────────────────────
# 编译结果展示
# 扫描 bin/ 目录下的可执行文件，列出每个文件的名称和大小
# ──────────────────────────────────────────────
print_result() {
    echo ""
    echo -e "${BOLD}─────────────────────────────────────────────────────${RESET}"
    echo -e "${BOLD}  编译产物${RESET}"
    echo -e "${BOLD}─────────────────────────────────────────────────────${RESET}"

    if [[ -d "${BIN_DIR}" ]]; then
        local count=0
        for f in "${BIN_DIR}"/*; do
            if [[ -f "${f}" && -x "${f}" ]]; then
                local size
                size=$(du -sh "${f}" 2>/dev/null | cut -f1)
                printf "  %-20s  %s\n" "$(basename "${f}")" "${size}"
                (( count++ )) || true
            fi
        done
        echo ""
        success "共 ${count} 个可执行文件 -> ${BIN_DIR}"
    else
        warn "未找到输出目录：${BIN_DIR}"
    fi
    echo ""
}

# ──────────────────────────────────────────────
# 策划表生成：DataDoc/*.xlsx -> database/*.lua
# 失败不阻断编译（可能尚未安装 openpyxl）
# ──────────────────────────────────────────────
gen_datadoc() {
    if [[ -x "${SCRIPT_DIR}/gen_data.sh" ]]; then
        info "生成策划配表 (DataDoc -> database)..."
        if "${SCRIPT_DIR}/gen_data.sh"; then
            success "配表生成完成"
        else
            warn "配表生成跳过，可稍后执行: ./gen_data.sh"
        fi
    fi
}

# ──────────────────────────────────────────────
# 主流程控制
# 执行顺序：参数解析 -> 依赖检查 -> 环境信息 -> (清理?) -> (cmake配置?) -> 编译 -> 结果展示
# ──────────────────────────────────────────────
main() {
    parse_args "$@"          # 步骤1：解析命令行参数

    check_deps               # 步骤2：检查工具链和第三方依赖
    gen_datadoc              # 步骤2b：Excel 配表 -> Lua
    print_env                 # 步骤3：打印构建环境信息摘要

    # 步骤4：处理 clean / rebuild 标志
    if [[ "${DO_CLEAN}" == true ]]; then
        do_clean              # 仅执行清理然后退出
        exit 0
    fi
    if [[ "${DO_REBUILD}" == true ]]; then
        do_clean              # 重建模式：先清理再继续后续流程
    fi

    # 步骤5：CMake 配置（仅在缓存不存在时执行，避免重复配置的开销）
    if [[ ! -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
        do_configure
    else
        info "检测到已有 cmake 缓存，跳过重新配置（如需重新配置请先执行：./build.sh clean）"
    fi

    # 步骤6：执行编译
    do_build

    # 步骤7：展示编译结果（生成的可执行文件列表）
    print_result
}

main "$@"
