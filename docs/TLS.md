# 全链路 TLS 加密

本文档说明 RPG Server 的 **TLS 1.2+ over TCP** 传输层加密：配置、证书、客户端接入与运维自检。  
应用层协议帧不变（4 字节 `MsgHeader` + body），见 [PROTOCOL.md](PROTOCOL.md)。

---

## 1. 概述

| 连接类型 | 端口示例 | 模式 |
|----------|----------|------|
| 客户端 → Login | 9010 | TLS（服务端证书，客户端校验 CA） |
| 客户端 → Gateway | 9005 | 同上 |
| 区内 S2S | 9000–9004 | **mTLS**（双向证书 + CA 校验） |
| Super ↔ Login 注册 | 19010 | mTLS |
| Super ↔ Logger/Global/Zone | 9006–9008 | mTLS |

**破坏性变更**：`config.xml` / `extern_*.xml` 中 `<Tls enabled="1">` 时，所有进程与 **RPG_Client** 必须同时启用 TLS；明文 TCP 将被拒绝（握手失败并断开）。

实现位于 `sdk/net/`：`TlsConfig`、`TlsContext`、`TcpConnection`（非阻塞 `SSL_read`/`SSL_write`）、`NetTls.h`（`initNetTls` / `wireTlsServer` / `wireTlsClient`）。

---

## 2. 首次部署（开发环境）

TLS 证书**不入 Git**；`./pull.sh` 在缺失时会自动执行 `gen_tls_certs.sh`，也可手动：

```bash
./scripts/gen_tls_certs.sh          # 生成 config/tls/{ca,server}.{crt,key}
./Build.sh                          # 全量编译
./StopServer.sh && ./RunServer.sh   # 区内集群
./RunServer.sh login                # 外联 Login（RunServer 默认不含）
```

各进程启动日志应出现 `TLS enabled verifyPeer=1`（或等价 INFO）。  
客户端（RPG_Client，仓库外）需加载 **`config/tls/ca.crt`** 校验服务端；dev 可配置 `insecureSkipVerify`（仅调试）。

---

## 3. 配置

### 3.1 游戏区 — `config/config.xml`

```xml
<!-- 修改后需重启全部区内进程 -->
<Tls enabled="1"
     cert="config/tls/server.crt"
     key="config/tls/server.key"
     ca="config/tls/ca.crt"
     verifyPeer="1"
     minVersion="1.2"/>
```

各进程 `main.cpp` 调用 `ServerBootstrap::initNetTlsFromConfig(cfg)`；`Init()` 中对 `TcpServer`/`TcpClient` 调用 `wireTlsServer` / `wireTlsClient`。

### 3.2 外联服 — `extern_*.xml`

Login / Logger / Global / Zone 各自 `<ExternServer>` 下 `<Tls>` 段，字段与上表相同。  
Login 的 9010 与 19010 共用同一 `SSL_CTX`。

### 3.3 生产环境

- 运维替换 `config/tls/*.crt` / `*.key` 为正式证书（Let's Encrypt 或内网 CA 签发）。
- 路径不变，仅换文件；区内 mTLS 可为每进程独立 cert，须由同一 CA 签发。
- **回滚（仅 dev）**：`<Tls enabled="0"/>` 可恢复明文；生产默认 `enabled="1"`。

---

## 4. mTLS 策略

| 侧 | 行为 |
|----|------|
| **Server** | 加载 `cert`+`key`；`verifyPeer=1` 时 **区内/注册 accept** 要求对端客户端证书；**玩家客户端口**（Login 9010、Gateway 9005）使用单向 TLS（不要求客户端证书） |
| **Client** | 校验服务端证书链；出示同一 `server.crt/key` 作为客户端证书（dev 单证书双向复用） |
| **失败** | WARN 日志 + `Close()`，**不降级明文** |

### 4.1 密码套件

| 配置项 | 说明 |
|--------|------|
| `cipherSuites` | TLS 1.2 及以下 OpenSSL cipher list（默认仅 ECDHE + AES-GCM） |
| `tls13CipherSuites` | TLS 1.3 套件（默认 `TLS_AES_128_GCM_SHA256` / `TLS_AES_256_GCM_SHA384`） |

`TlsContext` 另禁用 SSLv2/SSLv3 与 TLS 压缩（`SSL_OP_NO_COMPRESSION`）。配置无效时进程 init 失败。

---

## 5. 客户端（RPG_Client）

1. TCP 连接后立刻进行 TLS 握手（OpenSSL / 平台 SSL API）。
2. 加载 `ca.crt` 校验服务端；dev 可跳过校验。
3. 握手成功后仍发送 **4 字节头 + body**（与明文时代相同）。
4. Login 9010 与 Gateway 9005 **均须 TLS**。

### 5.1 区列表（9010）接入清单

| 步骤 | 要求 |
|------|------|
| 连接 | `connect(serverIp, 9010)` 后立即 `SSL_connect` / 平台 TLS 包装 |
| CA | 加载仓库 `config/tls/ca.crt`（与 `./scripts/gen_tls_certs.sh` 生成的一致） |
| 客户端证书 | **不需要**（Login 9010 / Gateway 9005 为单向 TLS；仅校验服务端证书） |
| 请求 | `MsgHeader`: `bodyLen=N`, `module=0x00`, `sub=0x0B`；body 为 `C2SZoneListReq` Protobuf（`game_type=0xFF` 表示全部）；亦兼容 body 首字节单字节 gameType |
| 响应 | `sub=0x0C`；body 为 `S2CZoneListRsp` Protobuf（`repeated ZoneEntry`） |
| Common | 同步 RPG_Server `Common` 子模块（`ZoneMsg.proto` + `generated/`） |

**症状**：服务端 `login.log` 仅 `登录客户端 TLS 握手未完成即断开`、无 `登录客户端连接` → 客户端仍用明文 TCP。

**自检**（无需客户端）：

```bash
python3 scripts/test_zone_list_tls.py
grep "已下发区列表" logs/login.log
```

详见 [EXTERNAL.md](EXTERNAL.md) 两阶段登录与防火墙说明。

---

## 6. 自检

```bash
# TLS 握手（Login）
openssl s_client -connect 127.0.0.1:9010 -CAfile config/tls/ca.crt -brief </dev/null

# 端口 + TLS 脚本
./scripts/check_login_ports.sh 127.0.0.1

# 区列表 TLS 冒烟（9010，无需 Gateway）
python3 scripts/test_zone_list_tls.py

# E2E（同机，已 wrap TLS；需 Gateway 9005 运行）
python3 scripts/test_login_gateway_e2e.py
```

握手失败常见原因：未执行 `gen_tls_certs.sh`、证书路径错误、客户端未信任 CA、仅部分进程重启导致旧二进制/旧配置混跑。

---

## 7. 架构约束

- 单线程 `Poll()`：握手在连接建立时非阻塞完成，稳态 `SSL_read/write` 与 epoll 兼容。
- **禁止**在 handler 内绕过 `TcpConnection` 裸 `recv`/`send`。
- 不改 `MsgHeader`、Gateway `ClientMsgValidator` / 服间 `InternalMsg` 布局。

### 7.1 Super ↔ Login 注册口（19010）

Super 到 Login 注册口为 **单条 mTLS 长连接**，承载网关注册、心跳、区状态与 **票据校验**。实现要点：

| 约束 | 说明 |
|------|------|
| 串行写 | `LoginExternOutbox` 队列；校验 `push_front` 优先；同帧禁止二次 poll |
| 断线重试 | 外联断开时**在途校验退回队列**，预热后重发；日志 `外联断开，票据校验重排队`（非立即 fail Record） |
| 预热 | 重连后 1.5s 内不发 `LOGIN_VERIFY_TOKEN_REQ` |
| 半开检测 | `ExternalServerConnector::tickReconnect` 在 `IsConnected` 但 `!canSend()` 超过 3s 时强制断开重连 |
| 队列满 | 出站队列满（256）时 Super 主动向 Record 回 `REC_VERIFY_TOKEN_RSP code=1` |
| TLS 诊断 | `TcpConnection` 在 TLS 读写失败时输出 OpenSSL 错误（`TLS 读失败` / `TLS 写失败`），便于定位 19010 闪断 |

成功校验日志链：`登录外联: 票据校验入队` → `已转发票据校验` → `登录服收到票据校验` → `登录服票据校验成功`。

闪断重试日志链：`已转发票据校验` → `外联断开，票据校验重排队` →（重连+预热）→ `已转发票据校验` → `登录服收到票据校验`。

相关文档：[ARCHITECTURE.md](ARCHITECTURE.md) · [SERVERS.md](SERVERS.md) · [EXTERNAL.md](EXTERNAL.md)
