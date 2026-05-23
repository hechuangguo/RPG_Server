#!/usr/bin/env bash
# =============================================================================
#  build.sh  —  RPG Server 统一编译脚本
#
#  用法：
#    ./build.sh               # 默认 Release 编译，并发数 = CPU 核数
#    ./build.sh -j10          # 指定 10 并发
#    ./build.sh debug         # Debug 模式（不优化，ASAN 开启）
#    ./build.sh clean         # 清除构建目录
#    ./build.sh rebuild       # 先 clean 再全量编译
#    ./build.sh SuperServer   # 只编译指定服务器（支持多个，空格分隔）
#    ./build.sh -j8 debug     # 8 并发 Debug 模式
#    ./build.sh -j8 SceneServer RecordServer  # 8 并发编译指定服务器
#
#  参数说明：
#    -j<N>              make 并发数，默认自动检测 CPU 核数
#    debug              编译类型为 Debug（RelWithDebInfo 为 Release）
#    clean              删除 build/ 目录
#    rebuild            等价于 clean + 全量编译
#    <ServerName>...    只构建指定目标（空格分隔，可多个）
# =============================================================================

set -euo pipefail

# ──────────────────────────────────────────────
# 颜色输出
# ──────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; BOLD='\033[1m'; RESET='\033[0m'

info()    { echo -e "${CYAN}[INFO]${RESET}  $*"; }
success() { echo -e "${GREEN}[OK]${RESET}    $*"; }
warn()    { echo -e "${YELLOW}[WARN]${RESET}  $*"; }
error()   { echo -e "${RED}[ERR]${RESET}   $*" >&2; exit 1; }

# ──────────────────────────────────────────────
# 路径基准
# ──────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
BIN_DIR="${BUILD_DIR}/bin"

# ──────────────────────────────────────────────
# 所有合法服务器名称
# ──────────────────────────────────────────────
ALL_SERVERS=(
    SuperServer SessionServer RecordServer AOIServer
    SceneServer GatewayServer LoggerServer GlobalServer ZoneServer
)

# ──────────────────────────────────────────────
# 默认参数
# ──────────────────────────────────────────────
JOBS=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
BUILD_TYPE="RelWithDebInfo"      # Release 带调试符号
DO_CLEAN=false
DO_REBUILD=false
TARGETS=()                       # 空表示构建全部

# ──────────────────────────────────────────────
# 解析命令行参数
# ──────────────────────────────────────────────
parse_args() {
    for arg in "$@"; do
        case "${arg}" in
            -j[0-9]*)
                JOBS="${arg#-j}"
                ;;
            debug|Debug|DEBUG)
                BUILD_TYPE="Debug"
                ;;
            release|Release|RELEASE)
                BUILD_TYPE="Release"
                ;;
            clean|Clean|CLEAN)
                DO_CLEAN=true
                ;;
            rebuild|Rebuild|REBUILD)
                DO_REBUILD=true
                ;;
            *)
                # 检查是否是合法服务器名称
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
# 检查工具依赖
# ──────────────────────────────────────────────
check_deps() {
    local missing=()
    for tool in cmake make g++ pkg-config; do
        if ! command -v "${tool}" &>/dev/null; then
            missing+=("${tool}")
        fi
    done
    if [[ ${#missing[@]} -gt 0 ]]; then
        error "缺少必要工具：${missing[*]}
  Ubuntu/Debian 安装：sudo apt install build-essential cmake libmysqlclient-dev liblua5.4-dev libtinyxml2-dev
  CentOS/RHEL 安装：sudo yum install gcc-c++ cmake mysql-devel lua-devel tinyxml2-devel"
    fi
}

# ──────────────────────────────────────────────
# 打印环境信息
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
# cmake 配置阶段
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
# make 编译阶段
# ──────────────────────────────────────────────
do_build() {
    local start_ts end_ts elapsed

    # 构建 make 目标列表
    local make_targets=()
    if [[ ${#TARGETS[@]} -gt 0 ]]; then
        make_targets=("${TARGETS[@]}")
    fi
    # 空 make_targets 表示 make all

    info "开始编译 (make -j${JOBS})..."
    start_ts=$(date +%s)

    if [[ ${#make_targets[@]} -gt 0 ]]; then
        # 指定目标
        cmake --build "${BUILD_DIR}" \
              --parallel "${JOBS}" \
              --target "${make_targets[@]}" \
              2>&1 | tee "${BUILD_DIR}/build.log"
    else
        # 全量编译
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
# 输出编译结果
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
        success "共 ${count} 个可执行文件 → ${BIN_DIR}"
    else
        warn "未找到输出目录：${BIN_DIR}"
    fi
    echo ""
}

# ──────────────────────────────────────────────
# 主流程
# ──────────────────────────────────────────────
main() {
    parse_args "$@"

    check_deps
    print_env

    # clean / rebuild
    if [[ "${DO_CLEAN}" == true ]]; then
        do_clean
        exit 0
    fi
    if [[ "${DO_REBUILD}" == true ]]; then
        do_clean
    fi

    # cmake 配置（如果 build 目录不存在，或 CMakeCache.txt 不存在）
    if [[ ! -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
        do_configure
    else
        info "检测到已有 cmake 缓存，跳过重新配置（如需重新配置请先执行：./build.sh clean）"
    fi

    # 编译
    do_build

    # 打印结果
    print_result
}

main "$@"
