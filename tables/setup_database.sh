#!/bin/bash
# ============================================================
#  一键初始化 rpg_game 库、rpg_table 账号与表结构
#  用法：
#    ./tables/setup_database.sh              # 提示输入 root 密码
#    MYSQL_ROOT_PASSWORD=xxx ./tables/setup_database.sh
# ============================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
CRED="$SCRIPT_DIR/database.credentials"

# shellcheck disable=SC1090
[ -f "$CRED" ] && . "$CRED"

DB_HOST=${DB_HOST:-127.0.0.1}
DB_PORT=${DB_PORT:-3306}
DB_NAME=${DB_NAME:-rpg_game}
DB_USER=${DB_USER:-rpg_table}
DB_PASS=${DB_PASS:-rpg_table}

mysql_root() {
    if [ -n "${MYSQL_ROOT_PASSWORD:-}" ]; then
        mysql -h "$DB_HOST" -P "$DB_PORT" -u root -p"$MYSQL_ROOT_PASSWORD" "$@"
    else
        mysql -h "$DB_HOST" -P "$DB_PORT" -u root -p "$@"
    fi
}

mysql_app() {
    mysql -h "$DB_HOST" -P "$DB_PORT" -u "$DB_USER" -p"$DB_PASS" "$@"
}

if ! command -v mysql >/dev/null 2>&1; then
    echo "错误: 未找到 mysql 客户端，请先安装 MySQL/MariaDB。" >&2
    exit 1
fi

if ! mysql_root -e "SELECT 1" >/dev/null 2>&1; then
    echo "错误: 无法连接 MySQL（请确认服务已启动且 root 密码正确）。" >&2
    echo "  示例: sudo systemctl start mysqld" >&2
    echo "  示例: MYSQL_ROOT_PASSWORD=你的root密码 $0" >&2
    exit 1
fi

echo "[1/3] 创建库 rpg_game 与用户 ${DB_USER} ..."
mysql_root < "$SCRIPT_DIR/create_user_and_db.sql"

echo "[2/3] 建表 (tables/init.sql) ..."
mysql_root < "$SCRIPT_DIR/init.sql"

echo "[3/3] 验证 ${DB_USER} 可连接 ${DB_NAME} ..."
mysql_app -e "USE ${DB_NAME}; SHOW TABLES;"

echo "完成。连接信息见 tables/database.credentials，config/config.xml 已对齐。"
