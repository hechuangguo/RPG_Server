#!/bin/bash
# ============================================================
#  一键初始化三库（rpg_login / rpg_game / rpg_global）、rpg_table 账号与表结构
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
LOGIN_DB_NAME=${LOGIN_DB_NAME:-rpg_login}
GLOBAL_DB_NAME=${GLOBAL_DB_NAME:-rpg_global}

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

echo "[1/7] 创建三库与用户 ${DB_USER} ..."
mysql_root < "$SCRIPT_DIR/create_user_and_db.sql"

echo "[2/7] 建表 (tables/init.sql) ..."
mysql_root < "$SCRIPT_DIR/init.sql"

echo "[3/7] 验证 ${DB_USER} 可连接 ${DB_NAME}（游戏区 6 表）..."
mysql_app -e "USE ${DB_NAME}; SHOW TABLES;"

echo "[4/7] 验证 ${DB_USER} 可连接 ${LOGIN_DB_NAME}（登录服 3 表：GameUser/ZoneInfo/LoginSession）..."
mysql_app -e "USE ${LOGIN_DB_NAME}; SHOW TABLES;"

echo "[5/7] 验证 ${DB_USER} 可连接 ${GLOBAL_DB_NAME}（全局服 1 表）..."
mysql_app -e "USE ${GLOBAL_DB_NAME}; SHOW TABLES;"

echo "[6/7] 登录流程存量迁移 (alter_login_flow.sql) ..."
mysql_root < "$SCRIPT_DIR/alter_login_flow.sql"

echo "[7/7] LoginSession 唯一约束 (migrate_login_session_unique.sql) ..."
mysql_root < "$SCRIPT_DIR/migrate_login_session_unique.sql"

echo "[校验] CharBase.accid 列与 LoginSession.uk_accid_zone ..."
accid_ok=$(mysql_root -N -e "
SELECT COUNT(*) FROM information_schema.COLUMNS
WHERE TABLE_SCHEMA='${DB_NAME}' AND TABLE_NAME='CharBase' AND COLUMN_NAME='accid';")
uk_ok=$(mysql_root -N -e "
SELECT COUNT(*) FROM information_schema.STATISTICS
WHERE TABLE_SCHEMA='${LOGIN_DB_NAME}' AND TABLE_NAME='LoginSession' AND INDEX_NAME='uk_accid_zone';")
if [[ "${accid_ok:-0}" -lt 1 ]]; then
    echo "错误: ${DB_NAME}.CharBase 缺少 accid 列，请检查 alter_login_flow.sql" >&2
    exit 1
fi
if [[ "${uk_ok:-0}" -lt 1 ]]; then
    echo "错误: ${LOGIN_DB_NAME}.LoginSession 缺少 uk_accid_zone，请检查 migrate_login_session_unique.sql" >&2
    exit 1
fi

echo "完成。区内服见 config/config.xml（${DB_NAME}）；登录服 extern_login.xml（${LOGIN_DB_NAME}）；全局服 extern_global.xml（${GLOBAL_DB_NAME}）。"
