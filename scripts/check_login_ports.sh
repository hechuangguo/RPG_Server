#!/bin/bash
# check_login_ports.sh — 登录两阶段端口自检（9010 Login + 9005 Gateway）
#
# 用法：
#   ./scripts/check_login_ports.sh
#   ./scripts/check_login_ports.sh 192.168.65.128

set -euo pipefail

LAN_IP="${1:-192.168.65.128}"
PORTS=(9010 9005)
FAIL=0

echo "=== Listening (expect 0.0.0.0) ==="
for p in "${PORTS[@]}"; do
    if ss -lntp 2>/dev/null | grep -q ":${p} "; then
        ss -lntp 2>/dev/null | grep ":${p} " || true
    else
        echo "NOT LISTENING: ${p}"
        FAIL=1
    fi
done
echo ""

echo "=== TCP probe ${LAN_IP} ==="
for p in "${PORTS[@]}"; do
    if nc -zv -w 2 "${LAN_IP}" "${p}" 2>&1; then
        echo "OK: ${LAN_IP}:${p}"
    else
        echo "FAIL: ${LAN_IP}:${p}"
        FAIL=1
    fi
done
echo ""

if command -v firewall-cmd >/dev/null 2>&1; then
    echo "=== firewalld (read-only; use sudo ./scripts/open_game_ports.sh --check) ==="
    if sudo -n firewall-cmd --list-ports 2>/dev/null; then
        for p in "${PORTS[@]}"; do
            if sudo -n firewall-cmd --list-ports 2>/dev/null | grep -qw "${p}/tcp"; then
                echo "firewalld OK: ${p}/tcp"
            else
                echo "firewalld MISSING: ${p}/tcp — run: sudo ./scripts/open_game_ports.sh"
                FAIL=1
            fi
        done
    else
        echo "Run manually: sudo ./scripts/open_game_ports.sh --check"
    fi
    echo ""
fi

echo "=== Windows client (run on Windows PowerShell) ==="
echo "Test-NetConnection -ComputerName ${LAN_IP} -Port 9010"
echo "Test-NetConnection -ComputerName ${LAN_IP} -Port 9005"
echo ""

if [[ "$FAIL" -eq 0 ]]; then
    echo "PASS: server-side port check"
else
    echo "FAIL: see above"
    exit 1
fi
