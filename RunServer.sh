#!/bin/bash
# ============================================================
#  RunServer.sh —— 按依赖顺序启动所有服务器
#  用法：./RunServer.sh [config_path] [scene_info_path]
#  默认配置：config/config.xml（包含各服务器 IP/端口等配置）
#  默认场景：config/server_info.xml（场景服务专用配置）
# ============================================================

# 任何命令失败立即退出，避免部分启动导致的不一致状态
set -e

# 脚本所在目录作为项目根目录，用于推导其他路径
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN_DIR="$SCRIPT_DIR/.build/bin"         # 编译后的二进制文件目录
LOG_DIR="$SCRIPT_DIR/logs"              # 日志输出目录（按服务器名分别保存）
CONFIG="$SCRIPT_DIR/config/config.xml"   # 主配置文件（各服务器通用）
SCENE_INFO="$SCRIPT_DIR/config/server_info.xml"  # 场景信息配置文件
PID_DIR="$SCRIPT_DIR/run"               # PID 文件目录（用于进程管理和守护检查）

# 创建日志和 PID 文件目录（不存在则自动创建）
mkdir -p "$LOG_DIR" "$PID_DIR"

# 终端颜色定义，便于区分日志级别
GREEN="\033[0;32m"
YELLOW="\033[1;33m"
RED="\033[0;31m"
NC="\033[0m"

# 日志输出函数
log_info()  { echo -e "${GREEN}[INFO]${NC}  $1"; }
log_warn()  { echo -e "${YELLOW}[WARN]${NC}  $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }

# -------------------------------------------------------
#  启动单个服务器
#  参数：$1=服务器名称  $2+=额外启动参数（如配置文件路径）
#
#  启动前检查：
#    1. 二进制文件是否存在（依赖检查）
#    2. 是否已有同名的守护进程在运行
#  启动后：
#    - 通过 PID 文件跟踪进程，便于后续停止/重启
#    - 等待 0.5 秒确保进程完成初始化
# -------------------------------------------------------
start_server() {
    local NAME=$1
    local BINARY="$BIN_DIR/$NAME"
    local PIDFILE="$PID_DIR/$NAME.pid"
    shift
    local ARGS="$@"

    # 依赖检查：确保目标二进制已编译存在
    if [ ! -f "$BINARY" ]; then
        log_error "Binary not found: $BINARY"
        return 1
    fi

    # 重复启动保护：检查 PID 文件是否存在且进程仍在运行
    if [ -f "$PIDFILE" ]; then
        local OLD_PID=$(cat "$PIDFILE")
        if kill -0 "$OLD_PID" 2>/dev/null; then
            log_warn "$NAME already running (pid=$OLD_PID)"
            return 0
        fi
    fi

    # 后台启动，将标准输出和错误都重定向到日志文件，
    # 实际业务日志由各服务器自行写入 super.log 等文件
    nohup "$BINARY" $ARGS > "$LOG_DIR/${NAME}_stdout.log" 2>&1 &
    local PID=$!
    echo $PID > "$PIDFILE"
    log_info "Started $NAME (pid=$PID)"
    sleep 0.5   # 给进程时间完成初始化（包括端口绑定、内存分配等）
}

# -------------------------------------------------------
#  启动顺序（严格按依赖关系排列）：
#
#  SuperServer → SessionServer → RecordServer/AOIServer/SceneServer
#  → GatewayServer → LoggerServer
#  → GlobalServer(可选) → ZoneServer(可选)
#
#  依赖说明：
#    • SuperServer：全局服务，无依赖，最先启动
#    • SessionServer：会话管理，依赖 SuperServer 注册服务
#    • RecordServer：数据记录，依赖 SessionServer 提供会话上下文
#    • AOIServer：AOI（Area Of Interest）可见性管理，依赖 SessionServer
#    • SceneServer：场景逻辑，依赖 SessionServer + SCENE_INFO 场景配置
#    • GatewayServer：客户端网关入口，依赖 SceneServer/RecordServer 已就绪
#    • LoggerServer：集中日志收集，依赖 SessionServer
#    • GlobalServer/ZoneServer：跨区/分区服务，可选启用
#
#  端口分配（在 config/config.xml 中配置）：
#    • SuperServer 端口在 config.xml 中静态配置，其他服务向 SuperServer
#      注册后获取各自的绑定端口（动态分配），避免端口冲突
# -------------------------------------------------------

log_info "===== RPG Server Startup ====="

# -------------------------------------------------------
#  第1步：启动 SuperServer
#  - 整个集群的服务注册中心，无依赖，必须最先启动
#  - 其他所有服务器启动时都会向 SuperServer 注册
# -------------------------------------------------------
start_server SuperServer "$CONFIG"
sleep 1

# -------------------------------------------------------
#  第2步：启动 SessionServer
#  - 管理客户端连接会话，登录/登出/心跳检测
#  - 依赖 SuperServer 已就绪才能完成服务注册
# -------------------------------------------------------
start_server SessionServer "$CONFIG"
sleep 1

# -------------------------------------------------------
#  第3步：并行启动 RecordServer、AOIServer、SceneServer
#  - RecordServer：处理用户数据持久化（存档、物品、属性等）
#  - AOIServer：管理玩家可见范围（AOI 九宫格算法核心）
#  - SceneServer：处理场景内逻辑（移动、战斗、NPC 交互等）
#    额外传入 SCENE_INFO 配置，支持多场景部署
# -------------------------------------------------------
start_server RecordServer "$CONFIG"
start_server AOIServer    "$CONFIG"
start_server SceneServer  "$CONFIG" "$SCENE_INFO"
sleep 1

# -------------------------------------------------------
#  第4步：启动 GatewayServer
#  - 客户端连接的唯一入口，负责协议转发和负载均衡
#  - 依赖 SceneServer/RecordServer 已注册，否则无法路由请求
# -------------------------------------------------------
start_server GatewayServer "$CONFIG"
sleep 0.5

# -------------------------------------------------------
#  第5步：启动 LoggerServer
#  - 集中收集各服务器的业务日志，写入统一日志文件
#  - 依赖 SessionServer 已就绪
# -------------------------------------------------------
start_server LoggerServer "$CONFIG"
sleep 0.5

# -------------------------------------------------------
#  第6步：可选服务器（默认不启用）
#  - 通过环境变量 ENABLE_GLOBAL=1 和 ENABLE_ZONE=1 控制
#  - GlobalServer：跨区服务，多区合服场景使用
#  - ZoneServer：分区管理服务，大区逻辑隔离场景使用
# -------------------------------------------------------
ENABLE_GLOBAL=${ENABLE_GLOBAL:-0}
ENABLE_ZONE=${ENABLE_ZONE:-0}

if [ "$ENABLE_GLOBAL" = "1" ]; then
    start_server GlobalServer "$CONFIG"
fi

if [ "$ENABLE_ZONE" = "1" ]; then
    start_server ZoneServer "$CONFIG"
fi

log_info "===== All servers started ====="
log_info "Log dir : $LOG_DIR"
log_info "PID dir : $PID_DIR"
log_info "Use './log.sh' to watch logs in real-time."
log_info "Use './StopServer.sh' to stop all servers."
