#!/usr/bin/env python3
"""
Login → Gateway 鉴权 → 角色列表 → 创角(可选) → 选角进世界 E2E 冒烟（TLS + Protobuf）。

依赖：
  pip install protobuf
  ./scripts/gen_proto_py.sh
"""

import hashlib
import os
import socket
import ssl
import struct
import sys
import time

_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(_SCRIPT_DIR)
_PB_DIR = os.path.join(_SCRIPT_DIR, "pb")
if _PB_DIR not in sys.path:
    sys.path.insert(0, _PB_DIR)

try:
    import LoginMsg_pb2
    import MapDataMsg_pb2
except ImportError as exc:
    print(
        "FAIL: 缺少 Python protobuf 生成物或 google.protobuf 包。\n"
        "  ./scripts/gen_proto_py.sh\n"
        "  pip install protobuf",
        file=sys.stderr,
    )
    raise SystemExit(2) from exc

HOST = "127.0.0.1"
LOGIN_PORT = 9010
GATEWAY_PORT = 9005

DEFAULT_CA = os.path.join(_ROOT, "config", "tls", "ca.crt")
TLS_INSECURE = os.environ.get("TLS_INSECURE", "").strip() in ("1", "true", "yes")

LOGIN_MODULE = 0
SCENE_MODULE = 1
C2S_LOGIN_REQ = 1
S2C_LOGIN_RSP = 2
S2C_LOGIN_CHALLENGE = 0x10
C2S_SELECT_USER_REQ = 5
S2C_USER_LIST = 6
C2S_CREATE_USER_REQ = 7
S2C_CREATE_USER_RSP = 8
S2C_ENTER_GAME = 9
S2C_GATEWAY_INFO = 10
C2S_GATEWAY_AUTH_REQ = 0x0D
C2S_LOGOUT_REQ = 0x0E
S2C_LOGOUT_RSP = 0x0F
S2C_SPAWN_ENTITY = 5
MSG_HEADER_SIZE = 4


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


_SSL_CTX = None


def tls_connect(host, port, timeout=5):
    """TCP 连接后 wrap TLS（与 Login/Gateway 一致）。"""
    global _SSL_CTX
    if _SSL_CTX is None:
        _SSL_CTX = make_ssl_context()
    raw = socket.create_connection((host, port), timeout)
    raw.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    return _SSL_CTX.wrap_socket(raw, server_hostname=host)


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


def send_msg(sock, module, sub, body: bytes):
    """发送一帧：MsgHeader(4B) + Protobuf body（body 内不含 module/sub）。"""
    sock.sendall(struct.pack("<HBB", len(body), module, sub) + body)


def password_digest(plain: str) -> bytes:
    """32 字节 SHA-256(UTF-8 密码)；不含 login_nonce。"""
    return hashlib.sha256(plain.encode("utf-8")).digest()


def recv_login_challenge(sock, timeout=5.0) -> bytes:
    """等待 S2CLoginChallenge 并返回 nonce 字节。"""
    reader = MsgReader(sock)
    deadline = time.time() + timeout
    while time.time() < deadline:
        for mod, sub, data in reader.poll(0.5):
            if mod == LOGIN_MODULE and sub == S2C_LOGIN_CHALLENGE:
                msg = LoginMsg_pb2.S2CLoginChallenge()
                if msg.ParseFromString(data) and len(msg.nonce) == 16:
                    return msg.nonce
    return b""


def fill_protocol_version(msg):
    """填充 protocol_version major=1 minor=0。"""
    pv = msg.protocol_version
    pv.major = 1
    pv.minor = 0


def parse_login_rsp(data: bytes):
    rsp = LoginMsg_pb2.S2CLoginRsp()
    if not rsp.ParseFromString(data):
        return None
    return {
        "code": rsp.code,
        "msg": rsp.msg,
        "userID": rsp.user_id,
        "accid": rsp.accid,
        "token": rsp.login_token,
    }


def parse_gateway_info(data: bytes):
    info = LoginMsg_pb2.S2CGatewayInfo()
    if not info.ParseFromString(data):
        return None, None
    return info.gateway_ip, info.gateway_port


def parse_user_list(data: bytes):
    lst = LoginMsg_pb2.S2CUserList()
    if not lst.ParseFromString(data):
        return None
    entries = []
    for e in lst.entries:
        entries.append(
            {
                "userID": e.user_id,
                "name": e.name,
                "level": e.level,
                "vocation": e.vocation,
                "sex": e.sex,
            }
        )
    return {"code": lst.code, "count": len(entries), "entries": entries}


def parse_create_user_rsp(data: bytes):
    rsp = LoginMsg_pb2.S2CCreateUserRsp()
    if not rsp.ParseFromString(data):
        return None
    return {"code": rsp.code, "userID": rsp.user_id}


def parse_enter_game(data: bytes):
    eg = LoginMsg_pb2.S2CEnterGame()
    if not eg.ParseFromString(data):
        return None
    return {
        "userID": eg.user_id,
        "map_id": eg.map_id,
        "x": eg.pos.x,
        "y": eg.pos.y,
        "z": eg.pos.z,
    }


def parse_logout_rsp(data: bytes):
    rsp = LoginMsg_pb2.S2CLogoutRsp()
    if not rsp.ParseFromString(data):
        return None
    return {"code": rsp.code, "action": rsp.action}


def login(account, password, zone_id=1, game_type=0):
    req = LoginMsg_pb2.C2SLoginReq()
    req.account = account
    req.password_digest = password_digest(password)
    req.zone_id = zone_id
    req.game_type = game_type
    fill_protocol_version(req)

    sock = tls_connect(HOST, LOGIN_PORT, 5)
    nonce = recv_login_challenge(sock, timeout=5.0)
    if len(nonce) != 16:
        sock.close()
        return None, None, None
    req.login_nonce = nonce
    send_msg(sock, LOGIN_MODULE, C2S_LOGIN_REQ, req.SerializeToString())
    reader = MsgReader(sock)
    msgs = reader.collect({S2C_LOGIN_RSP, S2C_GATEWAY_INFO}, timeout=5.0)
    sock.close()

    login_rsp = None
    gateway_ip = None
    gateway_port = None
    for mod, sub, data in msgs:
        if mod == LOGIN_MODULE and sub == S2C_LOGIN_RSP:
            login_rsp = parse_login_rsp(data)
        elif mod == LOGIN_MODULE and sub == S2C_GATEWAY_INFO:
            gateway_ip, gateway_port = parse_gateway_info(data)
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

    auth_req = LoginMsg_pb2.C2SGatewayAuthReq()
    auth_req.account = account
    auth_req.login_token = login_rsp["token"]
    auth_req.zone_id = 1
    auth_req.game_type = 0
    fill_protocol_version(auth_req)
    send_msg(gw, LOGIN_MODULE, C2S_GATEWAY_AUTH_REQ, auth_req.SerializeToString())

    pending = reader.collect({S2C_LOGIN_RSP, S2C_USER_LIST}, timeout=12.0)
    user_list = None
    auth_ok = False
    for mod, sub, data in pending:
        if mod == LOGIN_MODULE and sub == S2C_LOGIN_RSP:
            rsp = parse_login_rsp(data)
            if rsp:
                print(f"    S2C_LOGIN_RSP auth code={rsp['code']}")
                auth_ok = rsp["code"] == 0
        if mod == LOGIN_MODULE and sub == S2C_USER_LIST:
            user_list = parse_user_list(data)
            if user_list:
                print(f"    S2C_USER_LIST code={user_list['code']} count={user_list['count']}")

    if auth_ok and not user_list:
        extra = reader.collect({S2C_USER_LIST}, timeout=5.0)
        pending.extend(extra)
        for mod, sub, data in extra:
            if mod == LOGIN_MODULE and sub == S2C_USER_LIST:
                user_list = parse_user_list(data)
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
        create_req = LoginMsg_pb2.C2SCreateUserReq()
        create_req.name = role_name
        create_req.vocation = 0
        create_req.sex = 0
        send_msg(gw, LOGIN_MODULE, C2S_CREATE_USER_REQ, create_req.SerializeToString())

        pending = reader.collect({S2C_CREATE_USER_RSP}, timeout=12.0)
        create_ok = False
        for mod, sub, data in pending:
            if mod == LOGIN_MODULE and sub == S2C_CREATE_USER_RSP:
                create_rsp = parse_create_user_rsp(data)
                if not create_rsp:
                    continue
                print(f"    S2C_CREATE_USER_RSP code={create_rsp['code']} userID={create_rsp['userID']}")
                if create_rsp["code"] == 0 and create_rsp["userID"]:
                    user_id = create_rsp["userID"]
                    create_ok = True
        if not create_ok or not user_id:
            print("FAIL: create character")
            gw.close()
            return 1

        print("[3b] immediate select after create (ownedRoleIds fallback, skip USER_LIST wait) ...")
    print(f"[4] select userID={user_id} enter world ...")
    select_req = LoginMsg_pb2.C2SSelectUserReq()
    select_req.user_id = user_id
    select_req.login_txn_id = 0
    send_msg(gw, LOGIN_MODULE, C2S_SELECT_USER_REQ, select_req.SerializeToString())

    pending = reader.collect({S2C_ENTER_GAME, S2C_LOGIN_RSP}, timeout=20.0)
    enter_ok = False
    spawn_ok = False
    for mod, sub, data in pending:
        if mod == LOGIN_MODULE and sub == S2C_LOGIN_RSP:
            rsp = parse_login_rsp(data)
            if rsp:
                print(f"    S2C_LOGIN_RSP enter code={rsp['code']} userID={rsp['userID']}")
        if mod == LOGIN_MODULE and sub == S2C_ENTER_GAME:
            eg = parse_enter_game(data)
            if eg:
                print(
                    f"    S2C_ENTER_GAME userID={eg['userID']} map={eg['map_id']} "
                    f"pos=({eg['x']:.1f},{eg['y']:.1f},{eg['z']:.1f})"
                )
                enter_ok = True
        if mod == SCENE_MODULE and sub == S2C_SPAWN_ENTITY:
            spawn = MapDataMsg_pb2.S2CSpawnEntity()
            if spawn.ParseFromString(data):
                print(f"    S2C_SPAWN_ENTITY entity_id={spawn.entity_id} name={spawn.name}")
                spawn_ok = True

    if not enter_ok:
        gw.close()
        print("FAIL: no S2C_ENTER_GAME")
        return 1

    print(f"[5] logout return char select userID={user_id} ...")
    logout_req = LoginMsg_pb2.C2SLogoutReq()
    logout_req.action = LoginMsg_pb2.RETURN_CHAR_SELECT
    send_msg(gw, LOGIN_MODULE, C2S_LOGOUT_REQ, logout_req.SerializeToString())

    pending = reader.collect({S2C_LOGOUT_RSP, S2C_USER_LIST}, timeout=15.0)
    logout_ok = False
    relist_ok = False
    for mod, sub, data in pending:
        if mod == LOGIN_MODULE and sub == S2C_LOGOUT_RSP:
            lr = parse_logout_rsp(data)
            if lr:
                print(f"    S2C_LOGOUT_RSP code={lr['code']} action={lr['action']}")
                logout_ok = lr["code"] == 0 and lr["action"] == LoginMsg_pb2.RETURN_CHAR_SELECT
        if mod == LOGIN_MODULE and sub == S2C_USER_LIST:
            ul = parse_user_list(data)
            if ul:
                print(f"    S2C_USER_LIST after logout count={ul['count']}")
                relist_ok = ul["count"] > 0

    if not logout_ok or not relist_ok:
        gw.close()
        print("FAIL: logout return char select")
        return 1

    print(f"[6] re-select userID={user_id} enter world again ...")
    select_req = LoginMsg_pb2.C2SSelectUserReq()
    select_req.user_id = user_id
    select_req.login_txn_id = 0
    send_msg(gw, LOGIN_MODULE, C2S_SELECT_USER_REQ, select_req.SerializeToString())

    pending = reader.collect({S2C_ENTER_GAME}, timeout=20.0)
    reenter_ok = False
    for mod, sub, data in pending:
        if mod == LOGIN_MODULE and sub == S2C_ENTER_GAME:
            eg = parse_enter_game(data)
            if eg and eg["userID"] == user_id:
                print(f"    S2C_ENTER_GAME re-enter userID={eg['userID']}")
                reenter_ok = True

    gw.close()
    if reenter_ok:
        print(f"PASS: enter game + logout char select + re-enter OK spawn={'yes' if spawn_ok else 'no'}")
        return 0
    print("FAIL: re-enter after logout")
    return 1


if __name__ == "__main__":
    sys.exit(main())
