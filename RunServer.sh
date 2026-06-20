#!/bin/bash
# ============================================================
#  RunServer.sh —— 按依赖顺序启动 RPG 服务器
#
#  用法：
#    ./RunServer.sh              启动区内 6 服（Super → Record/AOI → Session → Scene → Gateway）
#    ./RunServer.sh all          同上
#    ./RunServer.sh <子命令>     仅启动指定服务器
#    ./RunServer.sh help         显示子命令列表
#
#  区内子命令：super | session | record | aoi | scene | gateway
#  外联子命令：logger | global | zone | login（须单独启动，如 ./RunServer.sh login）
#
#  二进制守护：./SceneServer/SceneServer -d（-d 后台运行，非配置路径）
#  配置默认：config/config.xml、config/server_info.xml（Scene）
#            外联服读各目录 extern_*.xml
# ============================================================

# 任何命令失败立即退出，避免部分启动导致的不一致状态
set -e
set -o pipefail

# 脚本所在目录作为项目根目录，用于推导其他路径
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOG_DIR="$SCRIPT_DIR/logs"              # 日志输出目录（按服务器名分别保存）
# 解析服务器二进制路径（优先服务器目录，其次兼容旧的 .build/bin）
resolve_server_binary() {
    local NAME=$1
    local CANDIDATE_NEW="$SCRIPT_DIR/$NAME/$NAME"
    local CANDIDATE_OLD="$SCRIPT_DIR/.build/bin/$NAME"
    if [ -f "$CANDIDATE_NEW" ]; then
        echo "$CANDIDATE_NEW"
    else
        echo "$CANDIDATE_OLD"
    fi
}

CONFIG="$SCRIPT_DIR/config/config.xml"   # 主配置文件（各服务器通用）
SCENE_INFO="$SCRIPT_DIR/config/server_info.xml"  # 场景信息配置文件
PID_DIR="$SCRIPT_DIR/run"               # PID 文件目录（用于进程管理和守护检查）
STARTUP_LOG_TAIL_LINES=${STARTUP_LOG_TAIL_LINES:-40}
STARTUP_WAIT_SEC=${STARTUP_WAIT_SEC:-3}           # 启动存活检测最长等待秒数
STARTUP_POLL_INTERVAL=${STARTUP_POLL_INTERVAL:-0.2} # 存活检测轮询间隔
SUPER_WARMUP_SEC=${SUPER_WARMUP_SEC:-5}         # Super 启动后等待 TLS 就绪再拉起子服

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

# 各服务器业务日志路径（与 config.xml LogPaths 一致）
server_log_path() {
    case "$1" in
        SuperServer)   echo "$LOG_DIR/super.log" ;;
        SessionServer) echo "$LOG_DIR/session.log" ;;
        RecordServer)  echo "$LOG_DIR/record.log" ;;
        AOIServer)     echo "$LOG_DIR/aoi.log" ;;
        SceneServer)   echo "$LOG_DIR/scene.log" ;;
        GatewayServer) echo "$LOG_DIR/gateway.log" ;;
        LoggerServer)  echo "$LOG_DIR/logger.log" ;;
        GlobalServer)  echo "$LOG_DIR/global.log" ;;
        ZoneServer)    echo "$LOG_DIR/zone.log" ;;
        LoginServer)   echo "$LOG_DIR/login.log" ;;
        *)             echo "" ;;
    esac
}

# 去掉 Logger 行首 [时间][Server][LEVEL] 前缀，便于阅读
strip_log_prefix() {
    sed -E 's/^\[[^]]+\]\[[^]]+\]\[(DEBUG|INFO|WARN|ERROR|FATAL)\] //'
}

# 从多个日志文件中提取最后一条匹配行（优先靠前的文件）
last_log_match() {
    local PATTERN=$1
    shift
    local FILE LINE
    for FILE in "$@"; do
        [ -f "$FILE" ] || continue
        LINE=$(grep -E "$PATTERN" "$FILE" 2>/dev/null | tail -1)
        if [ -n "$LINE" ]; then
            echo "$LINE"
            return 0
        fi
    done
    return 1
}

# 根据日志内容推断失败原因与修复建议
failure_hint_for() {
    local MSG=$1
    if echo "$MSG" | grep -qiE 'MySQL|mysql_|DB init failed|SQL err'; then
        echo "Database error: verify MySQL is running, run tables/init.sql, and check Database in config/config.xml."
    elif echo "$MSG" | grep -qiE 'Address already in use|EADDRINUSE|bind.*failed|listen failed|Start failed'; then
        echo "Port bind failed: run ./StopServer.sh, or change the port in config/config.xml."
    elif echo "$MSG" | grep -qiE 'config|ConfigLoader|xml|parse'; then
        echo "Config load failed: check config/config.xml (and config/server_info.xml for SceneServer)."
    elif echo "$MSG" | grep -qiE 'Lua|luaL_|init\.lua|script/'; then
        echo "Lua load failed: ensure script/ exists and cwd is the project root."
    elif echo "$MSG" | grep -qiE 'SuperServer|connect.*super|register'; then
        echo "Upstream dependency failed: ensure SuperServer is running and superIP/superPort in config.xml are correct."
    elif echo "$MSG" | grep -qiE 'scene|server_info|mapID|loadResources'; then
        echo "Scene init failed: check config/server_info.xml and SceneServer map/script resources."
    elif echo "$MSG" | grep -qiE 'binary|not found|No such file'; then
        echo "Missing binary or resource: run ./build.sh first."
    else
        echo "See log excerpts below for details."
    fi
}

# 解析 stdout / 业务日志，返回可读失败原因与建议（两行）
diagnose_startup_failure() {
    local NAME=$1
    local FALLBACK=${2:-"process exited during startup"}
    local STDOUT_LOG="$LOG_DIR/${NAME}_stdout.log"
    local SERVER_LOG
    SERVER_LOG=$(server_log_path "$NAME")
    local RAW="" CLEAN="" HINT=""

    RAW=$(last_log_match '\[FATAL\]' "$STDOUT_LOG" "$SERVER_LOG" || true)
    if [ -z "$RAW" ]; then
        RAW=$(last_log_match '\[(ERROR|ERR)\]' "$STDOUT_LOG" "$SERVER_LOG" || true)
    fi
    if [ -z "$RAW" ]; then
        RAW=$(last_log_match '(failed|error|cannot|denied|Address already in use|EADDRINUSE)' "$STDOUT_LOG" "$SERVER_LOG" || true)
    fi

    if [ -n "$RAW" ]; then
        CLEAN=$(printf '%s\n' "$RAW" | strip_log_prefix)
        HINT=$(failure_hint_for "$RAW")
        printf '%s\n%s' "$CLEAN" "$HINT"
    else
        HINT=$(failure_hint_for "$FALLBACK")
        printf '%s\n%s' "$FALLBACK" "$HINT"
    fi
}

# 列出当前仍存活、已写入 PID 的服务
list_running_servers() {
    local PIDFILE NAME PID
    local ANY=0
    for PIDFILE in "$PID_DIR"/*.pid; do
        [ -f "$PIDFILE" ] || continue
        NAME=$(basename "$PIDFILE" .pid)
        PID=$(cat "$PIDFILE" 2>/dev/null)
        if [ -n "$PID" ] && kill -0 "$PID" 2>/dev/null; then
            log_warn "  still running: $NAME (pid=$PID)"
            ANY=1
        fi
    done
    [ "$ANY" -eq 0 ] && log_warn "  (none)"
}

# 打印日志文件尾部
print_log_tail() {
    local LABEL=$1
    local LOGFILE=$2
    log_error "----- $LABEL (last ${STARTUP_LOG_TAIL_LINES} lines) -----"
    if [ -f "$LOGFILE" ] && [ -s "$LOGFILE" ]; then
        tail -n "$STARTUP_LOG_TAIL_LINES" "$LOGFILE" | sed 's/^/  /'
    else
        log_error "  (empty or missing: $LOGFILE)"
    fi
}

# 启动失败时输出结构化诊断信息
show_startup_error() {
    local NAME=$1
    local FALLBACK=${2:-"process exited during startup"}
    local EXIT_CODE=${3:-unknown}
    local STDOUT_LOG="$LOG_DIR/${NAME}_stdout.log"
    local SERVER_LOG
    SERVER_LOG=$(server_log_path "$NAME")
    local CAUSE HINT

    mapfile -t _diag < <(diagnose_startup_failure "$NAME" "$FALLBACK")
    CAUSE="${_diag[0]:-$FALLBACK}"
    HINT="${_diag[1]:-}"

    log_error "===== $NAME startup failed ====="
    log_error "Exit code   : $EXIT_CODE"
    log_error "Likely cause: $CAUSE"
    [ -n "$HINT" ] && log_error "Suggestion  : $HINT"
    log_error "Log files   : $STDOUT_LOG${SERVER_LOG:+, $SERVER_LOG}"

    print_log_tail "$NAME stdout" "$STDOUT_LOG"
    if [ -n "$SERVER_LOG" ] && [ "$SERVER_LOG" != "$STDOUT_LOG" ]; then
        print_log_tail "$NAME server log" "$SERVER_LOG"
    fi
    log_error "----- end of $NAME diagnostics -----"

    log_error "Startup aborted. Already-started servers:"
    list_running_servers
    log_error "Run './StopServer.sh' to stop all servers before retrying."
}

# -------------------------------------------------------
#  启动单个服务器
#  参数：$1=服务器名称  $2+=额外启动参数（如配置文件路径）
#
#  启动前检查：
#    1. 二进制文件是否存在（依赖检查）
#    2. 是否已有同名的守护进程在运行
#  启动后：
#    - 通过 PID 文件跟踪进程，便于后续停止/重启
#    - 等待进程完成初始化并确认仍存活；失败则返回 1 中止后续启动
# -------------------------------------------------------
start_server() {
    local NAME=$1
    local BINARY
    BINARY=$(resolve_server_binary "$NAME")
    local PIDFILE="$PID_DIR/$NAME.pid"
    shift

    # 依赖检查：确保目标二进制已编译存在
    if [ ! -f "$BINARY" ]; then
        show_startup_error "$NAME" "binary not found: $BINARY"
        return 1
    fi

    # 重复启动保护：检查 PID 文件是否存在且进程仍在运行
    if [ -f "$PIDFILE" ]; then
        local OLD_PID
        OLD_PID=$(cat "$PIDFILE")
        if kill -0 "$OLD_PID" 2>/dev/null; then
            log_warn "$NAME already running (pid=$OLD_PID)"
            return 0
        fi
        rm -f "$PIDFILE"
    fi

    # 后台启动：子 shell 内 exec 到二进制，$! 即为服务器进程 PID（避免 nohup 子 shell 导致 PID 错位）
    # cwd 固定为项目根，便于 Lua 加载 script/database/basefile
    (cd "$SCRIPT_DIR" && exec "$BINARY" "$@") > "$LOG_DIR/${NAME}_stdout.log" 2>&1 &
    local PID=$!
    echo "$PID" > "$PIDFILE"
    log_info "Started $NAME (pid=$PID)"

    # 轮询等待初始化；进程提前退出则收集 exit code 并诊断日志
    local WAIT_ROUNDS
    WAIT_ROUNDS=$(awk -v w="$STARTUP_WAIT_SEC" -v i="$STARTUP_POLL_INTERVAL" 'BEGIN { printf "%d", (w / i) + 0.5 }')
    local ROUND=0
    while [ "$ROUND" -lt "$WAIT_ROUNDS" ]; do
        if ! kill -0 "$PID" 2>/dev/null; then
            break
        fi
        sleep "$STARTUP_POLL_INTERVAL"
        ROUND=$((ROUND + 1))
    done

    if ! kill -0 "$PID" 2>/dev/null; then
        local EXIT_CODE=unknown
        if wait "$PID" 2>/dev/null; then
            EXIT_CODE=0
        else
            EXIT_CODE=$?
        fi
        rm -f "$PIDFILE"
        show_startup_error "$NAME" "process exited during startup (pid=$PID)" "$EXIT_CODE"
        return 1
    fi
}

# -------------------------------------------------------
#  打印用法与子命令列表
# -------------------------------------------------------
print_usage() {
    cat <<EOF
Usage:
  ./RunServer.sh                 Start in-zone cluster (6 servers)
  ./RunServer.sh all             Same as default
  ./RunServer.sh <command>       Start one server only

In-zone commands:
  super    SuperServer
  session  SessionServer
  record   RecordServer
  aoi      AOIServer
  scene    SceneServer
  gateway  GatewayServer

External commands (start separately):
  logger   LoggerServer  (LoggerServer/extern_logger.xml)
  global   GlobalServer  (GlobalServer/extern_global.xml)
  zone     ZoneServer    (ZoneServer/extern_zone.xml)
  login    LoginServer   (LoginServer/extern_login.xml)

Or run external binary directly, e.g. ./LoginServer/LoginServer

Daemon mode (binary directly):
  ./SceneServer/SceneServer -d   Fork to background; -d is not a config path

Stop all: ./StopServer.sh
Logs:     ./log.sh
EOF
}

# -------------------------------------------------------
#  启动区内 6 服（Super → Record/AOI → Session → Scene → Gateway）
#  外联服（Login/Logger/Global/Zone）须单独：./RunServer.sh login 等
#  任一失败则中止并输出诊断信息
# -------------------------------------------------------
start_all_inzone() {
    log_info "===== RPG In-Zone Startup (6 servers) ====="

    start_server SuperServer "$CONFIG" || return 1
    log_info "Waiting ${SUPER_WARMUP_SEC}s for SuperServer TLS warmup..."
    sleep "$SUPER_WARMUP_SEC"

    start_server RecordServer "$CONFIG" || return 1
    start_server AOIServer    "$CONFIG" || return 1
    sleep 1

    start_server SessionServer "$CONFIG" || return 1
    sleep 1

    start_server SceneServer  "$CONFIG" "$SCENE_INFO" || return 1
    sleep 1

    start_server GatewayServer "$CONFIG" || return 1

    log_info "===== In-zone 6 servers started ====="
    log_info "External (optional): ./RunServer.sh login | logger | global | zone"
    return 0
}

# -------------------------------------------------------
#  按子命令启动单个服务器（不自动拉依赖；失败打印具体原因）
# -------------------------------------------------------
start_one_command() {
    local CMD
    CMD=$(echo "$1" | tr '[:upper:]' '[:lower:]')

    case "$CMD" in
        super)
            log_info "===== Start SuperServer ====="
            start_server SuperServer "$CONFIG"
            ;;
        session)
            log_info "===== Start SessionServer ====="
            start_server SessionServer "$CONFIG"
            ;;
        record)
            log_info "===== Start RecordServer ====="
            start_server RecordServer "$CONFIG"
            ;;
        aoi)
            log_info "===== Start AOIServer ====="
            start_server AOIServer "$CONFIG"
            ;;
        scene)
            log_info "===== Start SceneServer ====="
            start_server SceneServer "$CONFIG" "$SCENE_INFO"
            ;;
        gateway)
            log_info "===== Start GatewayServer ====="
            start_server GatewayServer "$CONFIG"
            ;;
        logger)
            log_info "===== Start LoggerServer (external) ====="
            start_server LoggerServer
            ;;
        global)
            log_info "===== Start GlobalServer (external) ====="
            start_server GlobalServer
            ;;
        zone)
            log_info "===== Start ZoneServer (external) ====="
            start_server ZoneServer
            ;;
        login)
            log_info "===== Start LoginServer (external) ====="
            start_server LoginServer
            ;;
        *)
            log_error "Unknown command: $1"
            echo ""
            print_usage
            return 1
            ;;
    esac
}

# -------------------------------------------------------
#  入口：无参/all → 区内 6 服；help → 用法；否则单服子命令
# -------------------------------------------------------
CMD=${1:-}

case "$CMD" in
    ""|all)
        start_all_inzone || exit 1
        ;;
    help|-h|--help)
        print_usage
        exit 0
        ;;
    *)
        start_one_command "$CMD" || exit 1
        ;;
esac

log_info "Log dir : $LOG_DIR"
log_info "PID dir : $PID_DIR"
log_info "Use './log.sh' to watch logs in real-time."
log_info "Use './StopServer.sh' to stop all servers."
