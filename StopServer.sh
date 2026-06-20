#!/bin/bash
# ============================================================
#  StopServer.sh —— 停止所有 RPG 服务器进程并释放端口
#  用法：./StopServer.sh  或  ./StopServer（兼容入口）
#
#  停止策略：
#    1. 按依赖反序对已知服务器发 SIGTERM（与 RunServer 启动顺序相反）
#    2. 扫描 run/*.pid 与项目目录下残留二进制进程（补杀孤儿/多开实例）
#    3. 等待 STOP_WAIT_SEC 秒；仍存活则 SIGKILL
#    4. 清理 run/*.pid，并打印区内 9000–9008 与 Login 9010/19010 端口占用
# ============================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PID_DIR="$SCRIPT_DIR/run"
STOP_WAIT_SEC=${STOP_WAIT_SEC:-8}

GREEN="\033[0;32m"
YELLOW="\033[1;33m"
RED="\033[0;31m"
NC="\033[0m"

log_stop()  { echo -e "${GREEN}[STOP]${NC}  $1"; }
log_warn()  { echo -e "${YELLOW}[WARN]${NC}  $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }

# 与 RunServer.sh / Build.sh 一致的服务器列表（反序停止）
ALL_SERVERS=(
    ZoneServer GlobalServer LoggerServer LoginServer GatewayServer
    SceneServer AOIServer RecordServer SessionServer SuperServer
)

# 收集某服务器在项目内的所有匹配 PID（新目录与旧 .build/bin 均匹配）
collect_server_pids() {
    local NAME=$1
    local pid seen=""
    local line

  for pattern in \
      "${SCRIPT_DIR}/${NAME}/${NAME}" \
      "${SCRIPT_DIR}/.build/bin/${NAME}" \
      ".build/bin/${NAME}" \
      "/${NAME}/${NAME} "; do
        while IFS= read -r line; do
            [[ -z "$line" ]] && continue
            if [[ " $seen " != *" $line "* ]]; then
                seen="${seen} ${line}"
                echo "$line"
            fi
        done < <(pgrep -f "$pattern" 2>/dev/null || true)
    done
}

# 向 PID 发信号（忽略不存在/无权限）
signal_pids() {
    local SIG=$1
    shift
    local pid
    for pid in "$@"; do
        [[ -z "$pid" ]] && continue
        kill "-$SIG" "$pid" 2>/dev/null || true
    done
}

# 等待一组 PID 全部退出
wait_pids_gone() {
    local deadline=$((SECONDS + STOP_WAIT_SEC))
    local pid any_alive=1

    while [[ "$SECONDS" -lt "$deadline" ]]; do
        any_alive=0
        for pid in "$@"; do
            [[ -z "$pid" ]] && continue
            if kill -0 "$pid" 2>/dev/null; then
                any_alive=1
                break
            fi
        done
        [[ "$any_alive" -eq 0 ]] && return 0
        sleep 0.2
    done
    return 1
}

# 合并 PID 到全局数组（去重）
declare -a TARGET_PIDS=()

add_target_pid() {
    local pid=$1
    local existing
    [[ -z "$pid" ]] && return
    for existing in "${TARGET_PIDS[@]}"; do
        [[ "$existing" == "$pid" ]] && return
    done
    TARGET_PIDS+=("$pid")
}

# 从 PID 文件与进程扫描收集待停止 PID
for NAME in "${ALL_SERVERS[@]}"; do
    PIDFILE="$PID_DIR/$NAME.pid"
    if [[ -f "$PIDFILE" ]]; then
        add_target_pid "$(tr -d '[:space:]' < "$PIDFILE")"
    fi
    while IFS= read -r pid; do
        add_target_pid "$pid"
    done < <(collect_server_pids "$NAME")
done

# run/ 下其它 .pid（兼容手工写入）
for PIDFILE in "$PID_DIR"/*.pid; do
    [[ -f "$PIDFILE" ]] || continue
    add_target_pid "$(tr -d '[:space:]' < "$PIDFILE")"
done

if [[ ${#TARGET_PIDS[@]} -eq 0 ]]; then
    log_warn "未发现运行中的服务器进程。"
else
    log_stop "发送 SIGTERM 到 ${#TARGET_PIDS[@]} 个进程（等待最多 ${STOP_WAIT_SEC}s）..."
    signal_pids TERM "${TARGET_PIDS[@]}"

    if ! wait_pids_gone "${TARGET_PIDS[@]}"; then
        log_warn "部分进程未在 ${STOP_WAIT_SEC}s 内退出，发送 SIGKILL..."
        signal_pids KILL "${TARGET_PIDS[@]}"
        sleep 0.5
    fi

    for pid in "${TARGET_PIDS[@]}"; do
        if kill -0 "$pid" 2>/dev/null; then
            log_error "仍存活 pid=$pid ($(ps -p "$pid" -o args= 2>/dev/null || echo unknown))"
        fi
    done
fi

# 清理 PID 文件（含残留）
rm -f "$PID_DIR"/*.pid 2>/dev/null || true

# 按名称汇报是否还有残留进程
echo ""
echo "进程检查："
for NAME in "${ALL_SERVERS[@]}"; do
    remaining=$(collect_server_pids "$NAME" | tr '\n' ' ')
    if [[ -n "$remaining" ]]; then
        log_error "$NAME 仍在运行: $remaining"
    else
        log_stop "$NAME 已停止"
    fi
done

# 默认端口占用（区内 config.xml + LoginServer extern_login.xml）
echo ""
echo "端口检查 (9000-9008, Login 9010/19010)："
if command -v ss &>/dev/null; then
    ss -ltnp 2>/dev/null | grep -E ':900[0-8]\b|:9010\b|:19010\b' || echo "  (无监听)"
elif command -v netstat &>/dev/null; then
    netstat -ltnp 2>/dev/null | grep -E ':900[0-8]\b|:9010\b|:19010\b' || echo "  (无监听)"
else
    log_warn "未找到 ss/netstat，跳过端口检查"
fi

echo ""
echo "All servers stopped."
