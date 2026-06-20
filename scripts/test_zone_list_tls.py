#!/usr/bin/env python3
"""LoginServer 9010 区列表 TLS 冒烟：C2S_ZONE_LIST_REQ → S2C_ZONE_LIST_RSP。"""

import os
import socket
import ssl
import struct
import sys
import time

HOST = os.environ.get("LOGIN_HOST", "127.0.0.1")
LOGIN_PORT = int(os.environ.get("LOGIN_PORT", "9010"))

_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(_SCRIPT_DIR)
DEFAULT_CA = os.path.join(_ROOT, "config", "tls", "ca.crt")
TLS_INSECURE = os.environ.get("TLS_INSECURE", "").strip() in ("1", "true", "yes")

LOGIN_MODULE = 0
C2S_ZONE_LIST_REQ = 0x0B
S2C_ZONE_LIST_RSP = 0x0C
ZONE_LIST_ALL_GAME_TYPES = 0xFF
MSG_HEADER_SIZE = 4
ZONE_LIST_HEADER_SIZE = 8  # module(1)+sub(1)+code(4)+count(2)
ZONE_ENTRY_SIZE = 112


def make_ssl_context():
    """构建客户端 TLS 上下文；默认校验 config/tls/ca.crt。"""
    if TLS_INSECURE:
        ctx = ssl.create_default_context()
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE
        return ctx
    cafile = os.environ.get("TLS_CAFILE", DEFAULT_CA)
    if not os.path.isfile(cafile):
        print(f"FAIL: CA not found: {cafile} (run ./scripts/gen_tls_certs.sh)", file=sys.stderr)
        sys.exit(2)
    return ssl.create_default_context(cafile=cafile)


def tls_connect(host, port, timeout=5):
    """TCP 连接后 wrap TLS（与 Login 9010 一致）。"""
    ctx = make_ssl_context()
    raw = socket.create_connection((host, port), timeout)
    raw.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    return ctx.wrap_socket(raw, server_hostname=host)


def send_msg(sock, module, sub, body):
    """发送一帧：MsgHeader(4B) + body（module/sub 在头中，body 仅含业务字段）。"""
    sock.sendall(struct.pack("<HBB", len(body), module, sub) + body)


def recv_zone_list(sock, timeout=10.0):
    """等待 S2C_ZONE_LIST_RSP 并解析 header。"""
    sock.settimeout(timeout)
    buf = b""
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            chunk = sock.recv(8192)
            if not chunk:
                break
            buf += chunk
        except socket.timeout:
            continue
        while len(buf) >= MSG_HEADER_SIZE:
            body_len, mod, sub = struct.unpack_from("<HBB", buf, 0)
            total = MSG_HEADER_SIZE + body_len
            if len(buf) < total:
                break
            body = buf[MSG_HEADER_SIZE:total]
            buf = buf[total:]
            if mod == LOGIN_MODULE and sub == S2C_ZONE_LIST_RSP:
                return parse_zone_list_rsp(body)
    return None


def parse_zone_list_rsp(body):
    """解析 S2C_ZONE_LIST_RSP body（wire v2 前缀可选）。"""
    off = 0
    if len(body) >= 2 and body[0] == LOGIN_MODULE and body[1] == S2C_ZONE_LIST_RSP:
        off = 2
    if len(body) < off + 6:
        return None
    code = struct.unpack_from("<i", body, off)[0]
    count = struct.unpack_from("<H", body, off + 4)[0]
    entries = []
    entry_off = off + 6
    for _ in range(count):
        if entry_off + ZONE_ENTRY_SIZE > len(body):
            break
        zone_id = struct.unpack_from("<I", body, entry_off)[0]
        game_type = body[entry_off + 4]
        enabled = body[entry_off + 5]
        name = body[entry_off + 6 : entry_off + 38].split(b"\x00")[0].decode("utf-8", "replace")
        ip = body[entry_off + 38 : entry_off + 102].split(b"\x00")[0].decode("utf-8", "replace")
        super_port = struct.unpack_from("<H", body, entry_off + 102)[0]
        entries.append(
            {
                "zoneId": zone_id,
                "gameType": game_type,
                "enabled": enabled,
                "name": name,
                "ip": ip,
                "superPort": super_port,
            }
        )
        entry_off += ZONE_ENTRY_SIZE
    return {"code": code, "count": count, "entries": entries}


def main():
    print(f"Connecting TLS {HOST}:{LOGIN_PORT} ...")
    sock = tls_connect(HOST, LOGIN_PORT)
    try:
        req_body = struct.pack("<B", ZONE_LIST_ALL_GAME_TYPES)
        send_msg(sock, LOGIN_MODULE, C2S_ZONE_LIST_REQ, req_body)
        rsp = recv_zone_list(sock)
        if rsp is None:
            print("FAIL: no S2C_ZONE_LIST_RSP within timeout", file=sys.stderr)
            return 1
        if rsp["code"] != 0:
            print(f"FAIL: zone list code={rsp['code']}", file=sys.stderr)
            return 1
        if rsp["count"] < 1:
            print(f"FAIL: zone list count={rsp['count']} (expected >= 1)", file=sys.stderr)
            return 1
        first = rsp["entries"][0] if rsp["entries"] else {}
        print(
            f"OK: zone list count={rsp['count']} "
            f"first zoneId={first.get('zoneId')} name={first.get('name')!r} ip={first.get('ip')!r}"
        )
        return 0
    finally:
        sock.close()


if __name__ == "__main__":
    sys.exit(main())
