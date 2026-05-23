#!/bin/bash
# ============================================================
#  RunServer.sh —— 按依赖顺序启动所有服务器
#  用法：./RunServer.sh [config_path] [scene_info_path]
# ============================================================

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN_DIR="$SCRIPT_DIR/build/bin"
LOG_DIR="$SCRIPT_DIR/logs"
CONFIG="$SCRIPT_DIR/config/config.xml"
SCENE_INFO="$SCRIPT_DIR/config/server_info.xml"
PID_DIR="$SCRIPT_DIR/run"

mkdir -p "$LOG_DIR" "$PID_DIR"

# 颜色输出
GREEN="\033[0;32m"
YELLOW="\033[1;33m"
RED="\033[0;31m"
NC="\033[0m"

log_info()  { echo -e "${GREEN}[INFO]${NC}  $1"; }
log_warn()  { echo -e "${YELLOW}[WARN]${NC}  $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }

# 启动单个服务器
start_server() {
    local NAME=$1
    local BINARY="$BIN_DIR/$NAME"
    local PIDFILE="$PID_DIR/$NAME.pid"
    shift
    local ARGS="$@"

    if [ ! -f "$BINARY" ]; then
        log_error "Binary not found: $BINARY"
        return 1
    fi

    if [ -f "$PIDFILE" ]; then
        local OLD_PID=$(cat "$PIDFILE")
        if kill -0 "$OLD_PID" 2>/dev/null; then
            log_warn "$NAME already running (pid=$OLD_PID)"
            return 0
        fi
    fi

    nohup "$BINARY" $ARGS > "$LOG_DIR/${NAME}_stdout.log" 2>&1 &
    local PID=$!
    echo $PID > "$PIDFILE"
    log_info "Started $NAME (pid=$PID)"
    sleep 0.5   # 给进程时间完成初始化
}

# -------------------------------------------------------
#  启动顺序：
#  SuperServer → SessionServer → RecordServer/AOIServer/SceneServer
#  → GatewayServer → LoggerServer
#  → GlobalServer(可选) → ZoneServer(可选)
# -------------------------------------------------------

log_info "===== RPG Server Startup ====="

# 1. SuperServer（无依赖，最先启动）
start_server SuperServer "$CONFIG"
sleep 1

# 2. SessionServer（依赖 SuperServer）
start_server SessionServer "$CONFIG"
sleep 1

# 3. RecordServer、AOIServer、SceneServer 并行（均依赖 SessionServer）
start_server RecordServer "$CONFIG"
start_server AOIServer    "$CONFIG"
start_server SceneServer  "$CONFIG" "$SCENE_INFO"
sleep 1

# 4. GatewayServer（依赖 SceneServer/RecordServer）
start_server GatewayServer "$CONFIG"
sleep 0.5

# 5. LoggerServer（依赖 SessionServer）
start_server LoggerServer "$CONFIG"
sleep 0.5

# 6. 可选服务器
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
