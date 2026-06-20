#!/usr/bin/env python3
"""Login → Gateway 鉴权 → 角色列表 → 创角(可选) → 选角进世界 E2E 冒烟（TLS）。"""

import hashlib
import os
import socket
import ssl
import struct
import sys
import time

HOST = "127.0.0.1"
LOGIN_PORT = 9010
GATEWAY_PORT = 9005

_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(_SCRIPT_DIR)
DEFAULT_CA = os.path.join(_ROOT, "config", "tls", "ca.crt")
TLS_INSECURE = os.environ.get("TLS_INSECURE", "").strip() in ("1", "true", "yes")


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
    ctx = ssl.create_default_context(cafile=cafile)
    return ctx


_SSL_CTX = None


def tls_connect(host, port, timeout=5):
    """TCP 连接后 wrap TLS（与 Login/Gateway 一致）。"""
    global _SSL_CTX
    if _SSL_CTX is None:
        _SSL_CTX = make_ssl_context()
    raw = socket.create_connection((host, port), timeout)
    raw.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    return _SSL_CTX.wrap_socket(raw, server_hostname=host)

LOGIN_MODULE = 0
SCENE_MODULE = 1
C2S_LOGIN_REQ = 1
S2C_LOGIN_RSP = 2
C2S_SELECT_USER_REQ = 5
S2C_USER_LIST = 6
C2S_CREATE_USER_REQ = 7
S2C_CREATE_USER_RSP = 8
S2C_ENTER_GAME = 9
C2S_LOGOUT_REQ = 0x0E
S2C_LOGOUT_RSP = 0x0F
C2S_GATEWAY_AUTH_REQ = 0x0D
S2C_SPAWN_ENTITY = 5

LOGIN_RSP_TOKEN_OFF = 86  # Msg_S2C_LoginRsp: module(1)+sub(1)+code(4)+msg(64)+userID(8)+accid(8)
LOGIN_RSP_MIN = LOGIN_RSP_TOKEN_OFF + 65  # 151；须覆盖 loginToken[65]
USER_LIST_HEADER_SIZE = 8  # Msg_S2C_UserListHeader: module(1)+sub(1)+code(4)+count(2)
USER_LIST_ENTRY_SIZE = 48
CREATE_USER_RSP_SIZE = 78  # Msg_S2C_CreateUserRsp: module(1)+sub(1)+code(4)+msg(64)+userID(8)
CREATE_USER_RSP_USERID_OFF = 70  # body[0:1]module/sub + [2:5]code + [6:69]msg；wire v2 前缀即 struct 前两字节，非额外 +2
CREATE_USER_FMT = "<BB32sBB2x"
SELECT_USER_FMT = "<BBQQ"
GATEWAY_AUTH_FMT = "<BB32s65sIB3x"
LOGOUT_REQ_FMT = "<BBB3x"
LOGOUT_RSP_MIN = 74  # module(1)+sub(1)+code(4)+action(1)+reserved(3)+msg(64)
MSG_HEADER_SIZE = 4  # MsgHeader: bodyLen(2)+module(1)+sub(1)，见 sdk/net/NetDefine.h


def wirePrefixOff(body, hdrMod, hdrSub):
    """body 前两字节与帧头 module/sub 一致时跳过 wire v2 前缀。"""
    if len(body) >= 2 and body[0] == hdrMod and body[1] == hdrSub:
        return 2
    return 0


class MsgReader:
    """TCP 粘包拆包缓冲。"""

    def __init__(self, sock):
        self.sock = sock
        self.buf = b""

    def poll(self, timeout=0.5):
        self.sock.settimeout(timeout)
        out = []
        try:
            chunk = self.sock.recv(8192)
            if chunk:
                self.buf += chunk
        except socket.timeout:
            pass
        while len(self.buf) >= MSG_HEADER_SIZE:
            body_len, mod, sub = struct.unpack_from("<HBB", self.buf, 0)
            total = MSG_HEADER_SIZE + body_len
            if len(self.buf) < total:
                break
            data = self.buf[MSG_HEADER_SIZE:total]
            self.buf = self.buf[total:]
            out.append((mod, sub, data))
        return out

    def collect(self, want_subs, timeout=15.0):
        deadline = time.time() + timeout
        pending = []
        got = set()
        while time.time() < deadline and not want_subs.issubset(got):
            for msg in self.poll(0.5):
                pending.append(msg)
                got.add(msg[1])
        return pending


def send_msg(sock, module, sub, body):
    """发送一帧：MsgHeader(4B) + body。wire v2 要求 body 前两字节也为 module/sub（与头一致）。"""
    sock.sendall(struct.pack("<HBB", len(body), module, sub) + body)


def pad_str(s, n):
    b = s.encode("utf-8")[: n - 1]
    return b.ljust(n, b"\x00")


def parse_login_rsp(data, hdrMod=LOGIN_MODULE, hdrSub=S2C_LOGIN_RSP):
    w = wirePrefixOff(data, hdrMod, hdrSub)
    if w == 2 or (len(data) >= 2 and data[0] == hdrMod and data[1] == hdrSub):
        if len(data) < LOGIN_RSP_MIN:
            return None
        return {
            "code": struct.unpack_from("<i", data, 2)[0],
            "msg": data[6:70].split(b"\x00")[0].decode("utf-8", "replace"),
            "userID": struct.unpack_from("<Q", data, 70)[0],
            "accid": struct.unpack_from("<Q", data, 78)[0],
            "token": data[LOGIN_RSP_TOKEN_OFF : LOGIN_RSP_MIN]
            .split(b"\x00")[0]
            .decode("ascii"),
        }
    if len(data) >= 84 + 65:
        # 兼容无 body 前缀的旧格式：code(4)+msg(64)+userID(8)+accid(8)+loginToken(65)
        return {
            "code": struct.unpack_from("<i", data, 0)[0],
            "msg": data[4:68].split(b"\x00")[0].decode("utf-8", "replace"),
            "userID": struct.unpack_from("<Q", data, 68)[0],
            "accid": struct.unpack_from("<Q", data, 76)[0],
            "token": data[84:149].split(b"\x00")[0].decode("ascii"),
        }
    return None


def parse_user_list(data, hdrMod=LOGIN_MODULE, hdrSub=S2C_USER_LIST):
    w = wirePrefixOff(data, hdrMod, hdrSub)
    if w == 2 or (len(data) >= USER_LIST_HEADER_SIZE and data[0] == hdrMod and data[1] == hdrSub):
        if len(data) < USER_LIST_HEADER_SIZE:
            return None
        code = struct.unpack_from("<i", data, 2)[0]
        count = struct.unpack_from("<H", data, 6)[0]
        off = USER_LIST_HEADER_SIZE
    elif len(data) >= 6:
        # 兼容无 body 前缀的旧格式：code(4)+count(2)
        code = struct.unpack_from("<i", data, 0)[0]
        count = struct.unpack_from("<H", data, 4)[0]
        off = 6
    else:
        return None
    entries = []
    for _ in range(count):
        if off + USER_LIST_ENTRY_SIZE > len(data):
            break
        user_id = struct.unpack_from("<Q", data, off)[0]
        name = data[off + 8 : off + 40].split(b"\x00")[0].decode("utf-8", "replace")
        level = struct.unpack_from("<I", data, off + 40)[0]
        vocation = data[off + 44]
        sex = data[off + 45]
        entries.append(
            {"userID": user_id, "name": name, "level": level, "vocation": vocation, "sex": sex}
        )
        off += USER_LIST_ENTRY_SIZE
    return {"code": code, "count": count, "entries": entries}


def parse_create_user_rsp(data, hdrMod=LOGIN_MODULE, hdrSub=S2C_CREATE_USER_RSP):
    w = wirePrefixOff(data, hdrMod, hdrSub)
    if w == 2 or (len(data) >= 2 and data[0] == hdrMod and data[1] == hdrSub):
        if len(data) < CREATE_USER_RSP_SIZE:
            return None
        return {
            "code": struct.unpack_from("<i", data, 2)[0],
            "userID": struct.unpack_from("<Q", data, CREATE_USER_RSP_USERID_OFF)[0],
        }
    if len(data) >= 72:
        # 兼容无 body 前缀的旧格式：code(4)+msg(64)+userID(8)
        return {
            "code": struct.unpack_from("<i", data, 0)[0],
            "userID": struct.unpack_from("<Q", data, 68)[0],
        }
    return None


def password_digest(plain: str) -> bytes:
    """32 字节 SHA-256(UTF-8 密码)，与 Msg_C2S_LoginReq.passwordDigest 一致。"""
    return hashlib.sha256(plain.encode("utf-8")).digest()


def login(account, password, zone_id=1, game_type=0):
    body = struct.pack("<BB", LOGIN_MODULE, C2S_LOGIN_REQ)
    body += pad_str(account, 32)
    body += password_digest(password)
    body += struct.pack("<IB3x", zone_id, game_type)

    sock = tls_connect(HOST, LOGIN_PORT, 5)
    send_msg(sock, LOGIN_MODULE, C2S_LOGIN_REQ, body)
    reader = MsgReader(sock)
    msgs = reader.collect({S2C_LOGIN_RSP, 0x0A}, timeout=5.0)
    sock.close()

    login_rsp = None
    gateway_ip = None
    gateway_port = None
    for mod, sub, data in msgs:
        if mod == LOGIN_MODULE and sub == S2C_LOGIN_RSP:
            login_rsp = parse_login_rsp(data, mod, sub)
        elif mod == LOGIN_MODULE and sub == 0x0A and len(data) >= 38:
            w = wirePrefixOff(data, mod, sub)
            gateway_ip = data[w + 4 : w + 36].split(b"\x00")[0].decode("utf-8", "replace")
            gateway_port = struct.unpack_from("<H", data, w + 36)[0]
    return login_rsp, gateway_ip, gateway_port


def main():
    account = sys.argv[1] if len(sys.argv) > 1 else "autotest_e2e"
    password = sys.argv[2] if len(sys.argv) > 2 else "test1234"
    role_name = sys.argv[3] if len(sys.argv) > 3 else f"E2E侠{int(time.time()) % 100000}"

    print(f"[1] login {account} ...")
    login_rsp, gw_ip, gw_port = login(account, password)
    if not login_rsp:
        print("FAIL: no S2C_LOGIN_RSP")
        return 1
    print(
        f"    code={login_rsp['code']} accid={login_rsp['accid']} "
        f"tokenLen={len(login_rsp['token'])} gateway={gw_ip}:{gw_port}"
    )
    if login_rsp["code"] != 0 or len(login_rsp["token"]) != 64:
        print(f"FAIL: login failed: {login_rsp['msg']}")
        return 1

    gw_host = gw_ip if gw_ip and gw_ip not in ("0.0.0.0", "") else HOST
    gw_port = gw_port or GATEWAY_PORT

    print(f"[2] gateway connect {gw_host}:{gw_port} ...")
    gw = tls_connect(gw_host, gw_port, 5)
    reader = MsgReader(gw)
    auth_body = struct.pack(
        GATEWAY_AUTH_FMT,
        LOGIN_MODULE,
        C2S_GATEWAY_AUTH_REQ,
        pad_str(account, 32),
        pad_str(login_rsp["token"], 65),
        1,
        0,
    )
    send_msg(gw, LOGIN_MODULE, C2S_GATEWAY_AUTH_REQ, auth_body)

    pending = reader.collect({S2C_LOGIN_RSP, S2C_USER_LIST}, timeout=12.0)
    user_list = None
    auth_ok = False
    for mod, sub, data in pending:
        if mod == LOGIN_MODULE and sub == S2C_LOGIN_RSP:
            w = wirePrefixOff(data, mod, sub)
            code = struct.unpack_from("<i", data, w)[0]
            print(f"    S2C_LOGIN_RSP auth code={code}")
            auth_ok = code == 0
        if mod == LOGIN_MODULE and sub == S2C_USER_LIST:
            user_list = parse_user_list(data, mod, sub)
            if user_list:
                print(f"    S2C_USER_LIST code={user_list['code']} count={user_list['count']}")

    # 角色列表与鉴权回包可能分包到达，继续短等列表
    if auth_ok and not user_list:
        extra = reader.collect({S2C_USER_LIST}, timeout=5.0)
        pending.extend(extra)
        for mod, sub, data in extra:
            if mod == LOGIN_MODULE and sub == S2C_USER_LIST:
                user_list = parse_user_list(data, mod, sub)
                if user_list:
                    print(f"    S2C_USER_LIST code={user_list['code']} count={user_list['count']}")

    if not auth_ok:
        print("FAIL: gateway auth")
        gw.close()
        return 1

    user_id = None
    if user_list and user_list["code"] == 0 and user_list["count"] > 0:
        user_id = user_list["entries"][0]["userID"]

    if user_id is None:
        print(f"[3] create character name={role_name} vocation=0 ...")
        create_body = struct.pack(
            CREATE_USER_FMT,
            LOGIN_MODULE,
            C2S_CREATE_USER_REQ,
            pad_str(role_name, 32),
            0,
            0,
        )
        send_msg(gw, LOGIN_MODULE, C2S_CREATE_USER_REQ, create_body)
        pending = reader.collect({S2C_CREATE_USER_RSP}, timeout=12.0)
        create_ok = False
        for mod, sub, data in pending:
            if mod == LOGIN_MODULE and sub == S2C_CREATE_USER_RSP:
                create_rsp = parse_create_user_rsp(data, mod, sub)
                if not create_rsp:
                    continue
                code = create_rsp["code"]
                uid = create_rsp["userID"]
                print(f"    S2C_CREATE_USER_RSP code={code} userID={uid}")
                if code == 0 and uid:
                    user_id = uid
                    create_ok = True
            if mod == LOGIN_MODULE and sub == S2C_USER_LIST:
                ul = parse_user_list(data, mod, sub)
                if not ul:
                    continue
                print(f"    S2C_USER_LIST refresh count={ul['count']}")
                if ul["count"] > 0:
                    user_id = ul["entries"][0]["userID"]
                    create_ok = True
        if not create_ok or not user_id:
            print("FAIL: create character")
            gw.close()
            return 1

    print(f"[4] select userID={user_id} enter world ...")
    select_body = struct.pack(
        SELECT_USER_FMT,
        LOGIN_MODULE,
        C2S_SELECT_USER_REQ,
        user_id,
        0,
    )
    send_msg(gw, LOGIN_MODULE, C2S_SELECT_USER_REQ, select_body)

    pending = reader.collect({S2C_ENTER_GAME}, timeout=20.0)
    enter_ok = False
    spawn_ok = False
    for mod, sub, data in pending:
        if mod == LOGIN_MODULE and sub == S2C_LOGIN_RSP and len(data) >= 4:
            w = wirePrefixOff(data, mod, sub)
            code = struct.unpack_from("<i", data, w)[0]
            uid = struct.unpack_from("<Q", data, w + 68)[0] if len(data) >= w + 76 else 0
            print(f"    S2C_LOGIN_RSP enter code={code} userID={uid}")
        if mod == LOGIN_MODULE and sub == S2C_ENTER_GAME and len(data) >= 48:
            w = wirePrefixOff(data, mod, sub)
            uid = struct.unpack_from("<Q", data, w)[0]
            map_id = struct.unpack_from("<I", data, w + 8 + 32)[0]
            x, y, z = struct.unpack_from("<fff", data, w + 8 + 32 + 4)
            print(f"    S2C_ENTER_GAME userID={uid} map={map_id} pos=({x:.1f},{y:.1f},{z:.1f})")
            enter_ok = True
        if mod == SCENE_MODULE and sub == S2C_SPAWN_ENTITY:
            print(f"    S2C_SPAWN_ENTITY len={len(data)}")
            spawn_ok = True

    if not enter_ok:
        gw.close()
        print("FAIL: no S2C_ENTER_GAME")
        return 1

    print(f"[5] logout return char select userID={user_id} ...")
    logout_body = struct.pack(
        LOGOUT_REQ_FMT,
        LOGIN_MODULE,
        C2S_LOGOUT_REQ,
        1,  # RETURN_CHAR_SELECT
    )
    send_msg(gw, LOGIN_MODULE, C2S_LOGOUT_REQ, logout_body)

    pending = reader.collect({S2C_LOGOUT_RSP, S2C_USER_LIST}, timeout=15.0)
    logout_ok = False
    relist_ok = False
    for mod, sub, data in pending:
        if mod == LOGIN_MODULE and sub == S2C_LOGOUT_RSP and len(data) >= LOGOUT_RSP_MIN:
            w = wirePrefixOff(data, mod, sub)
            code = struct.unpack_from("<i", data, w)[0]
            action = data[w + 4] if len(data) > w + 4 else 0
            print(f"    S2C_LOGOUT_RSP code={code} action={action}")
            logout_ok = code == 0 and action == 1
        if mod == LOGIN_MODULE and sub == S2C_USER_LIST:
            ul = parse_user_list(data, mod, sub)
            if ul:
                print(f"    S2C_USER_LIST after logout count={ul['count']}")
                relist_ok = ul["count"] > 0

    if not logout_ok or not relist_ok:
        gw.close()
        print("FAIL: logout return char select")
        return 1

    print(f"[6] re-select userID={user_id} enter world again ...")
    select_body = struct.pack(
        SELECT_USER_FMT,
        LOGIN_MODULE,
        C2S_SELECT_USER_REQ,
        user_id,
        0,
    )
    send_msg(gw, LOGIN_MODULE, C2S_SELECT_USER_REQ, select_body)

    pending = reader.collect({S2C_ENTER_GAME}, timeout=20.0)
    reenter_ok = False
    for mod, sub, data in pending:
        if mod == LOGIN_MODULE and sub == S2C_ENTER_GAME and len(data) >= 48:
            w = wirePrefixOff(data, mod, sub)
            uid = struct.unpack_from("<Q", data, w)[0]
            print(f"    S2C_ENTER_GAME re-enter userID={uid}")
            reenter_ok = uid == user_id

    gw.close()
    if reenter_ok:
        print(f"PASS: enter game + logout char select + re-enter OK spawn={'yes' if spawn_ok else 'no'}")
        return 0
    print("FAIL: re-enter after logout")
    return 1


if __name__ == "__main__":
    sys.exit(main())
