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
echo "生产配置校验通过: $CONFIG"
exit 0
