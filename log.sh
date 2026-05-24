#!/bin/bash
# ============================================================
#  log.sh —— 实时滚动输出所有服务器最新日志（tail logs/*.log）
#  依赖：tail（原生自带），tmux 可选
# ============================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOG_DIR="$SCRIPT_DIR/logs"

# 所有日志文件列表
LOG_FILES=(
    "$LOG_DIR/super.log"
    "$LOG_DIR/session.log"
    "$LOG_DIR/record.log"
    "$LOG_DIR/aoi.log"
    "$LOG_DIR/scene.log"
    "$LOG_DIR/gateway.log"
    "$LOG_DIR/logger.log"
    "$LOG_DIR/global.log"
    "$LOG_DIR/zone.log"
)

# 过滤出已存在的日志文件
EXISTING=()
for f in "${LOG_FILES[@]}"; do
    if [ -f "$f" ]; then
        EXISTING+=("$f")
    fi
done

if [ ${#EXISTING[@]} -eq 0 ]; then
    echo "No log files found in $LOG_DIR. Have you started the servers?"
    exit 1
fi

echo "=== Watching logs (Ctrl+C to exit) ==="
for f in "${EXISTING[@]}"; do
    echo "  $f"
done
echo ""

# 合并 tail，加上文件名前缀
tail -F "${EXISTING[@]}" 2>/dev/null | \
awk '
/^==> .* <==$/ {
    match($0, /==> (.*) <==$/, arr)
    current = arr[1]
    sub(/.*\//, "", current)   # 只保留文件名
    next
}
{
    if (current != "") {
        printf "[%-20s] %s\n", current, $0
    } else {
        print
    }
}
'
