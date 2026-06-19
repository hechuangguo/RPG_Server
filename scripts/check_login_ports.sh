#!/bin/bash
# check_login_ports.sh — 登录两阶段端口自检（9010 Login + 9005 Gateway，TLS）
#
# 用法：
#   ./scripts/check_login_ports.sh
#   ./scripts/check_login_ports.sh 192.168.65.128
#   CAFILE=config/tls/ca.crt ./scripts/check_login_ports.sh 127.0.0.1

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
LAN_IP="${1:-192.168.65.128}"
CAFILE="${CAFILE:-${ROOT}/config/tls/ca.crt}"
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
        echo "OK (TCP): ${LAN_IP}:${p}"
    else
        echo "FAIL (TCP): ${LAN_IP}:${p}"
        FAIL=1
    fi
done
echo ""

echo "=== TLS handshake ${LAN_IP} (CA: ${CAFILE}) ==="
if [[ ! -f "${CAFILE}" ]]; then
    echo "MISSING CA: ${CAFILE} — run: ./scripts/gen_tls_certs.sh"
    FAIL=1
elif ! command -v openssl >/dev/null 2>&1; then
    echo "SKIP TLS: openssl not installed"
else
    for p in "${PORTS[@]}"; do
        if openssl s_client -connect "${LAN_IP}:${p}" -CAfile "${CAFILE}" -brief </dev/null 2>&1 | grep -qE 'CONNECTION ESTABLISHED|Verification: OK'; then
            echo "OK (TLS): ${LAN_IP}:${p}"
        else
            echo "FAIL (TLS): ${LAN_IP}:${p} — is LoginServer/Gateway running with Tls enabled=1?"
            openssl s_client -connect "${LAN_IP}:${p}" -CAfile "${CAFILE}" -brief </dev/null 2>&1 | tail -5 || true
            FAIL=1
        fi
    done
fi
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
echo "Client must use TLS and trust ca.crt — see docs/TLS.md"
echo ""

if [[ "$FAIL" -eq 0 ]]; then
    echo "PASS: server-side port + TLS check"
else
    echo "FAIL: see above"
    exit 1
fi
