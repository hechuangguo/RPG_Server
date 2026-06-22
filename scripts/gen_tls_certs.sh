#!/usr/bin/env bash
# ============================================================
#  gen_tls_certs.sh —— 生成本地 dev 用 TLS 自签证书（CA + 服务端/mTLS）
#
#  用法：
#    ./scripts/gen_tls_certs.sh [输出目录，默认 config/tls]
#    ./scripts/gen_tls_certs.sh --force [输出目录]
#
#  产物（均在 .gitignore，不入库）：
#    ca.crt / ca.key       — CA（ca.key 勿外泄）
#    server.crt / server.key — 全区进程共用（dev）；SAN 含 127.0.0.1 localhost
#
#  已存在完整四套文件时默认跳过；pull.sh 在缺失时自动调用本脚本。
#  客户端（RPG_Client）需信任本机 ca.crt 或 dev 跳过校验。
# ============================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
FORCE=false
OUT_DIR="${ROOT}/config/tls"
DAYS=3650
CN="RPG-Server-Dev"

usage() {
    sed -n '3,14p' "$0" | sed 's/^# \{0,1\}//'
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --force) FORCE=true; shift ;;
        -h|--help) usage; exit 0 ;;
        *) OUT_DIR="$1"; shift ;;
    esac
done

mkdir -p "${OUT_DIR}"

if ! command -v openssl >/dev/null 2>&1; then
    echo "ERR: openssl 未安装（CentOS: openssl；Ubuntu: openssl）" >&2
    exit 1
fi

tls_cert_ready() {
    [[ -f "${OUT_DIR}/ca.crt" && -f "${OUT_DIR}/ca.key" \
       && -f "${OUT_DIR}/server.crt" && -f "${OUT_DIR}/server.key" ]]
}

if [[ "${FORCE}" != true ]] && tls_cert_ready; then
    echo "[gen_tls] 证书已就绪，跳过（--force 可重新生成）"
    exit 0
fi

# 从 serverlist / 配置收集客户端可达 IP，写入证书 SAN（避免连 LAN IP 时校验失败）
SAN="DNS:localhost,IP:127.0.0.1"
append_san_ip() {
    local ip="$1"
    [[ -z "${ip}" ]] && return
    [[ "${SAN}" == *"IP:${ip}"* ]] && return
    SAN="${SAN},IP:${ip}"
}
for cfg in \
    "${ROOT}/LoginServer/serverlist.xml" \
    "${ROOT}/config/config.xml" \
    "${ROOT}/config/server_info.xml"; do
    [[ -f "${cfg}" ]] || continue
    while IFS= read -r ip; do
        append_san_ip "${ip}"
    done < <(grep -oE '192\.168\.[0-9]+\.[0-9]+|10\.[0-9]+\.[0-9]+\.[0-9]+' "${cfg}" | sort -u)
done
# serverlist.xml Zone ip="..." 属性
if [[ -f "${ROOT}/LoginServer/serverlist.xml" ]]; then
    while IFS= read -r ip; do
        append_san_ip "${ip}"
    done < <(grep -oP 'ip="\K[0-9.]+' "${ROOT}/LoginServer/serverlist.xml" 2>/dev/null || true)
fi
echo "[gen_tls] SAN=${SAN}"

echo "[gen_tls] 输出目录: ${OUT_DIR}"

# CA
if [[ ! -f "${OUT_DIR}/ca.key" ]]; then
    openssl genrsa -out "${OUT_DIR}/ca.key" 4096
fi
openssl req -x509 -new -nodes -key "${OUT_DIR}/ca.key" -sha256 -days "${DAYS}" \
    -subj "/CN=RPG-Dev-CA" -out "${OUT_DIR}/ca.crt"

# Server key + CSR config with SAN
openssl genrsa -out "${OUT_DIR}/server.key" 2048

cat > "${OUT_DIR}/server.cnf" <<EOF
[req]
default_bits = 2048
prompt = no
default_md = sha256
distinguished_name = dn
req_extensions = req_ext

[dn]
CN = ${CN}

[req_ext]
subjectAltName = ${SAN}
EOF

openssl req -new -key "${OUT_DIR}/server.key" -out "${OUT_DIR}/server.csr" -config "${OUT_DIR}/server.cnf"

cat > "${OUT_DIR}/server_ext.cnf" <<EOF
subjectAltName = ${SAN}
EOF

openssl x509 -req -in "${OUT_DIR}/server.csr" -CA "${OUT_DIR}/ca.crt" -CAkey "${OUT_DIR}/ca.key" \
    -CAcreateserial -out "${OUT_DIR}/server.crt" -days "${DAYS}" -sha256 \
    -extfile "${OUT_DIR}/server_ext.cnf"

chmod 600 "${OUT_DIR}/ca.key" "${OUT_DIR}/server.key"
rm -f "${OUT_DIR}/server.csr" "${OUT_DIR}/server.cnf" "${OUT_DIR}/server_ext.cnf" "${OUT_DIR}/ca.srl"

echo "[OK] 已生成:"
echo "  ${OUT_DIR}/ca.crt"
echo "  ${OUT_DIR}/server.crt"
echo "  ${OUT_DIR}/server.key"
echo "请将 ca.crt 配置到客户端；服务端 config.xml Tls 段指向上述路径。"
