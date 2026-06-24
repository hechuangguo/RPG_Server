#!/usr/bin/env bash
# =============================================================================
# run_smoke.sh — 区列表 TLS + 可选登录网关 E2E 冒烟
#
# 用法：
#   ./scripts/run_smoke.sh                    # 仅区列表（无需账号）
#   ./scripts/run_smoke.sh <账号> <密码>      # 区列表 + 登录进世界 E2E
#
# 环境：TLS_INSECURE=1 默认开启（dev 自签证书）；生产可设 TLS_INSECURE=0 并配置 CA。
# =============================================================================

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

export TLS_INSECURE="${TLS_INSECURE:-1}"

echo "[smoke] 区列表 TLS 冒烟..."
python3 scripts/test_zone_list_tls.py

if [[ $# -ge 2 ]]; then
    echo "[smoke] 登录网关 E2E: account=$1"
    python3 scripts/test_login_gateway_e2e.py "$1" "$2"
else
    echo "[smoke] 跳过登录 E2E（传入 <账号> <密码> 可启用）"
fi

echo "[smoke] 全部通过"
