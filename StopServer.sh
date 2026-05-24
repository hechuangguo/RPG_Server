#!/bin/bash
# ============================================================
#  StopServer.sh —— 停止所有服务器
# ============================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PID_DIR="$SCRIPT_DIR/run"
GREEN="\033[0;32m"
RED="\033[0;31m"
NC="\033[0m"

SERVERS=(ZoneServer GlobalServer LoggerServer GatewayServer SceneServer AOIServer RecordServer SessionServer SuperServer)

for NAME in "${SERVERS[@]}"; do
    PIDFILE="$PID_DIR/$NAME.pid"
    if [ -f "$PIDFILE" ]; then
        PID=$(cat "$PIDFILE")
        if kill -0 "$PID" 2>/dev/null; then
            kill -TERM "$PID"
            echo -e "${GREEN}[STOP]${NC}  $NAME (pid=$PID)"
        else
            echo -e "${RED}[DEAD]${NC}  $NAME (pid=$PID) not running"
        fi
        rm -f "$PIDFILE"
    fi
done

echo "All servers stopped."
