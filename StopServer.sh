#!/bin/bash
# ============================================================
#  StopServer.sh —— 停止所有服务器
#  用法：./StopServer.sh
#
#  停止策略：
#    1. 优先优雅关闭（kill -TERM = SIGTERM = 信号15）
#       - 进程收到 TERM 信号后执行清理：保存数据、释放连接、通知注销
#       - 给予进程处理时间，避免数据丢失
#    2. 配合 RunServer.sh 的启动顺序：采用反向依赖顺序停止
#       - 上层服务先停，底层服务后停，避免级联错误
#    3. 不使用强制 kill（kill -9 = SIGKILL = 信号9）
#       - SIGKILL 无法被捕获，进程立即终止，可能导致数据损坏
#       - 如需强制停止，可在脚本外手动执行 kill -9 <pid>
#
#  进程匹配方式：通过 PID 文件精确匹配（非进程名匹配）
#    - RunServer.sh 启动时将 PID 写入 run/<name>.pid
#    - 本脚本读取 PID 文件获取进程号，避免误杀同名进程
#    - 停止后删除 PID 文件，防止下次误读僵尸 PID
# ============================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PID_DIR="$SCRIPT_DIR/run"       # PID 文件存放目录（与 RunServer.sh 中一致）

# 终端颜色
GREEN="\033[0;32m"
RED="\033[0;31m"
NC="\033[0m"

# -------------------------------------------------------
#  停止顺序：与启动顺序相反（先启的后停）
#
#  启动顺序：SuperServer → SessionServer → Record/Scene/AOI
#           → Gateway → Logger → Global → Zone
#  停止顺序：Zone → Global → Logger → Gateway
#           → Scene/AOI/Record → Session → SuperServer
#
#  原因：
#    - Zone/Global 是可选服务，先停它们减少对核心链路的影响
#    - Gateway 先停，防止新客户端连接进入
#    - Scene/Record/AOI 是业务核心，在 Gateway 断开后安全关闭
#    - Session 管理连接会话，在业务服务关闭后清理
#    - SuperServer 是注册中心，必须最后停，确保所有服务都能正常注销
# -------------------------------------------------------
SERVERS=(ZoneServer GlobalServer LoggerServer GatewayServer SceneServer AOIServer RecordServer SessionServer SuperServer)

for NAME in "${SERVERS[@]}"; do
    PIDFILE="$PID_DIR/$NAME.pid"

    # 检查 PID 文件是否存在（服务是否被 RunServer.sh 成功启动过）
    if [ -f "$PIDFILE" ]; then
        PID=$(cat "$PIDFILE")

        # 检查进程是否仍在运行（PID 文件可能存在但进程已退出）
        # kill -0：发送空信号，仅检测进程是否存在，不做任何操作
        if kill -0 "$PID" 2>/dev/null; then
            # 优雅关闭：发送 SIGTERM，进程可捕获后执行清理逻辑
            kill -TERM "$PID"
            echo -e "${GREEN}[STOP]${NC}  $NAME (pid=$PID)"
        else
            # 进程已退出但 PID 文件残留（可能是崩溃或之前未清理）
            echo -e "${RED}[DEAD]${NC}  $NAME (pid=$PID) not running"
        fi
        # 清理 PID 文件，避免下次运行时误判为正在运行
        rm -f "$PIDFILE"
    fi
done

echo "All servers stopped."
