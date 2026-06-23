#!/usr/bin/env bash
# 生产环境配置校验：TLS 强约束与数据库密码
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CONFIG="${RPG_CONFIG:-$ROOT/config/config.xml}"

export RPG_PRODUCTION=1

if [[ ! -f "$CONFIG" ]]; then
  echo "错误: 找不到配置文件 $CONFIG" >&2
  exit 1
fi

tls_enabled=$(grep -oP '(?<=enabled=")[^"]+' "$CONFIG" | head -1 || true)
verify_peer=$(grep -oP '(?<=verifyPeer=")[^"]+' "$CONFIG" | head -1 || true)
db_pass=$(grep -oP '(?<=pass=")[^"]+' "$CONFIG" | head -1 || true)

err=0
if [[ "$tls_enabled" != "1" ]]; then
  echo "生产环境禁止 TLS enabled=0" >&2
  err=1
fi
if [[ "$verify_peer" != "1" ]]; then
  echo "生产环境禁止 TLS verifyPeer=0" >&2
  err=1
fi
if [[ -z "$db_pass" || "$db_pass" == "rpg_table" || "$db_pass" == "root" ]]; then
  echo "生产环境禁止使用默认或空数据库密码" >&2
  err=1
fi

if [[ $err -ne 0 ]]; then
  exit 1
fi

if command -v mysql >/dev/null 2>&1; then
  db_host=$(grep -oP '(?<=host=")[^"]+' "$CONFIG" | head -1 || echo "127.0.0.1")
  db_user=$(grep -oP '(?<=user=")[^"]+' "$CONFIG" | head -1 || echo "rpg_table")
  db_pass=$(grep -oP '(?<=pass=")[^"]+' "$CONFIG" | head -1 || echo "")
  if mysql -h "$db_host" -u "$db_user" -p"$db_pass" -N -e \
    "SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA='rpg_game' AND TABLE_NAME='CharBase' AND COLUMN_NAME='accid';" 2>/dev/null | grep -q '^1$'; then
    echo "数据库迁移校验: CharBase.accid 已存在"
  else
    echo "警告: rpg_game.CharBase 缺少 accid，请执行 tables/setup_database.sh 或 alter_login_flow.sql" >&2
    err=1
  fi
fi

if [[ $err -ne 0 ]]; then
  exit 1
fi
echo "生产配置校验通过: $CONFIG"
exit 0
