#!/bin/bash
# open_game_ports.sh — 放行 Login(9010) + Gateway(9005) 客户端口（firewalld）
#
# 两阶段登录：Windows 客户端先连 9010 拿 token，再连 9005 鉴权/拉角色列表。
# 仅开 9010 会导致「账号登录成功、获取角色列表超时」。
#
# 用法（需 root）：
#   sudo ./scripts/open_game_ports.sh
#   sudo ./scripts/open_game_ports.sh --check   # 只查看，不改规则

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

PORTS=(9010 9005)
CHECK_ONLY=0

for arg in "$@"; do
    case "$arg" in
        --check) CHECK_ONLY=1 ;;
        -h|--help)
            echo "Usage: sudo $0 [--check]"
            exit 0
            ;;
    esac
done

if ! command -v firewall-cmd >/dev/null 2>&1; then
    echo "firewall-cmd not found; skip (no firewalld?)."
    exit 0
fi

if [[ "$(id -u)" -ne 0 ]]; then
    echo "Need root. Run: sudo $0"
    exit 1
fi

if ! systemctl is-active --quiet firewalld 2>/dev/null; then
    echo "firewalld is not active; nothing to do."
    exit 0
fi

echo "=== Current firewalld ports ==="
firewall-cmd --list-ports || true
echo ""

if [[ "$CHECK_ONLY" -eq 1 ]]; then
    missing=0
    for p in "${PORTS[@]}"; do
        if firewall-cmd --list-ports | grep -qw "${p}/tcp"; then
            echo "OK: ${p}/tcp open"
        else
            echo "MISSING: ${p}/tcp"
            missing=1
        fi
    done
    exit "$missing"
fi

for p in "${PORTS[@]}"; do
    firewall-cmd --permanent --add-port="${p}/tcp"
    echo "Added permanent rule: ${p}/tcp"
done

firewall-cmd --reload
echo ""
echo "=== After reload ==="
firewall-cmd --list-ports
echo ""
echo "Also ensure cloud security group allows inbound ${PORTS[*]}/tcp."
echo "Windows verify (PowerShell):"
echo "  Test-NetConnection -ComputerName <server-lan-ip> -Port 9010"
echo "  Test-NetConnection -ComputerName <server-lan-ip> -Port 9005"
