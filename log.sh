#!/bin/bash
# ============================================================
#  log.sh —— 实时滚动输出所有服务器最新日志
#
#  用法：
#    ./log.sh               # 查看所有已存在的日志文件
#
#  依赖：
#    - tail（Unix/Linux 原生自带）
#    - awk（Unix/Linux 原生自带）
#    - tmux 可选（用于分屏同时查看多个日志窗口）
#
#  日志文件来源：
#    各服务器（SuperServer、SessionServer 等）运行期间自行
#    写入 logs/ 目录下的对应日志文件
#
#  日志查看技巧：
#    • 按 Ctrl+C 退出实时监控
#    • 如需只看某个服务器日志：tail -F logs/super.log
#    • 如需 tmux 分屏查看：在一个窗口运行 ./log.sh，
#      另一个窗口运行 tail -F logs/scene.log 看特定日志
#    • 如需搜索关键词：./log.sh | grep ERROR
#
#  日志归档说明：
#    当前设计为持续写入不自动轮转。如需归档，建议方式：
#    1. 手动归档：
#       tar -czf logs_$(date +%Y%m%d_%H%M%S).tar.gz logs/
#    2. 使用 logrotate 自动轮转（推荐生产环境配置）：
#       创建 /etc/logrotate.d/rpg-server，内容示例：
#         /path/to/RPG/logs/*.log {
#             daily           # 每天轮转一次
#             rotate 7        # 保留最近7天
#             compress        # 压缩旧日志
#             missingok       # 文件不存在不报错
#             notifempty      # 空文件不轮转
#             copytruncate    # 复制后截断，不中断服务写入
#         }
#    3. 定时清理过期日志：
#       find logs/ -name "*.log" -mtime +7 -delete  # 删除7天前的日志
# ============================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOG_DIR="$SCRIPT_DIR/logs"

# 日志文件列表（按 RunServer.sh 启动顺序排列）
# 每个服务器对应一个业务日志文件：
#   super.log   → SuperServer  （全局注册中心日志）
#   session.log → SessionServer（会话管理日志）
#   record.log  → RecordServer （数据持久化日志）
#   aoi.log     → AOIServer    （可见性管理日志）
#   scene.log   → SceneServer  （场景逻辑日志）
#   gateway.log → GatewayServer（网关流量日志）
#   logger.log  → LoggerServer （集中收集日志）
#   login.log   → LoginServer   （分区管理日志）
#   global.log  → GlobalServer （跨区服务日志，可选）
#   zone.log    → ZoneServer   （分区管理日志，可选）
LOG_FILES=(
    "$LOG_DIR/super.log"
    "$LOG_DIR/session.log"
    "$LOG_DIR/record.log"
    "$LOG_DIR/aoi.log"
    "$LOG_DIR/scene.log"
    "$LOG_DIR/gateway.log"
    "$LOG_DIR/logger.log"
    "$LOG_DIR/login.log"
    "$LOG_DIR/global.log"
    "$LOG_DIR/zone.log"
)

# 过滤出已存在的日志文件（可选服务器未启用时，对应日志文件不存在）
EXISTING=()
for f in "${LOG_FILES[@]}"; do
    if [ -f "$f" ]; then
        EXISTING+=("$f")
    fi
done

# 没有任何日志文件时提示并退出（避免 tail 空参数报错）
if [ ${#EXISTING[@]} -eq 0 ]; then
    echo "No log files found in $LOG_DIR. Have you started the servers?"
    exit 1
fi

echo "=== Watching logs (Ctrl+C to exit) ==="
for f in "${EXISTING[@]}"; do
    echo "  $f"
done
echo ""

# -------------------------------------------------------
#  实时监控所有日志
#
#  tail -F（大写）vs tail -f（小写）的区别：
#    - -f：跟随文件末尾，但如果文件被删除/重命名（日志轮转），
#           跟踪会断开，不再输出新内容
#    - -F：等同 -f --retry，文件被删除后会自动重试重新打开，
#           适合配合 logrotate 等自动轮转工具使用
#
#  awk 处理逻辑：
#    1. 匹配 tail -F 输出的文件名分隔行（==> file <==），
#       提取文件名作为日志来源标签
#    2. 对常规日志行添加 [文件名] 前缀，区分日志来源
#    3. 无法识别的行原样输出
# -------------------------------------------------------
tail -F "${EXISTING[@]}" 2>/dev/null | \
awk '
# tail -F 在切换到不同文件时，会输出 "==> 文件路径 <==" 作为分隔
/^==> .* <==$/ {
    match($0, /==> (.*) <==$/, arr)
    current = arr[1]
    sub(/.*\//, "", current)   # 去掉路径，只保留文件名（如 gateway.log）
    next                       # 不输出分隔行本身
}
{
    if (current != "") {
        # 添加左对齐的20字符文件名前缀，便于对齐阅读
        printf "[%-20s] %s\n", current, $0
    } else {
        print   # 没有文件名上下文时直接输出
    }
}
'
