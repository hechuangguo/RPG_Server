# 全链路 TLS 加密

本文档说明 RPG Server 的 **TLS 1.2+ over TCP** 传输层加密：配置、证书、客户端接入与运维自检。  
应用层协议帧不变（6 字节 `MsgHeader` + body），见 [PROTOCOL.md](PROTOCOL.md)。

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
| **Server** | 加载 `cert`+`key`；`verifyPeer=1` 时要求对端出示客户端证书，CA 来自 `ca.crt` |
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
3. 握手成功后仍发送 **6 字节头 + body**（与明文时代相同）。
4. Login 9010 与 Gateway 9005 **均须 TLS**。

详见 [EXTERNAL.md](EXTERNAL.md) 两阶段登录与防火墙说明。

---

## 6. 自检

```bash
# TLS 握手（Login）
openssl s_client -connect 127.0.0.1:9010 -CAfile config/tls/ca.crt -brief </dev/null

# 端口 + TLS 脚本
./scripts/check_login_ports.sh 127.0.0.1

# E2E（同机，已 wrap TLS）
python3 scripts/test_login_gateway_e2e.py
```

握手失败常见原因：未执行 `gen_tls_certs.sh`、证书路径错误、客户端未信任 CA、仅部分进程重启导致旧二进制/旧配置混跑。

---

## 7. 架构约束

- 单线程 `Poll()`：握手在连接建立时非阻塞完成，稳态 `SSL_read/write` 与 epoll 兼容。
- **禁止**在 handler 内绕过 `TcpConnection` 裸 `recv`/`send`。
- 不改 `MsgHeader`、Gateway `ClientMsgValidator` / 服间 `InternalMsg` 布局。

相关文档：[ARCHITECTURE.md](ARCHITECTURE.md) · [SERVERS.md](SERVERS.md) · [EXTERNAL.md](EXTERNAL.md)
