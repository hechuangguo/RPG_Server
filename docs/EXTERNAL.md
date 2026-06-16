# 外联服务器架构

Logger、Global、Zone、Login 四个进程**不注册 SuperServer**，可部署在任意机器。  
区内 6 服只连 Super，经信封协议访问外联能力。

---

## 1. 拓扑

```mermaid
flowchart LR
    subgraph zone [游戏区内]
        GW[Gateway]
        SCE[Scene]
        SES[Session]
        REC[Record]
        AOI[AOI]
        SS[SuperServer]
    end
    subgraph extern [外联可选]
        LOG[LoggerServer]
        GLB[GlobalServer]
        ZON[ZoneServer]
        LS[LoginServer]
    end

    GW & SCE & SES & REC & AOI --> SS
    SS -->|ExternalServerHub| LOG & GLB & ZON & LS
    GW -. SS_LOGIN_GATEWAY_WRAP .-> SS -.-> LS
    SCE & SES -. SS_EXTERN_FWD .-> SS -.-> LOG & GLB & ZON & LS
```

**原则**：

- **仅 SuperServer** 读 `loginserverlist.xml` 并维护四条外联 TcpClient
- 区内服使用 `GameZoneExternSender` 发 `SS_EXTERN_FWD_REQ`
- Super `SuperExternRouter` 解包/封包转发
- 外联服用 `GameZoneMsgDispatch` 解 `EXT_GAMEZONE_FWD_REQ`

---

## 2. 配置文件

| 文件 | 读取者 | 内容 |
|------|--------|------|
| `loginserverlist.xml`（项目根） | SuperServer `ExternalServerHub` | Logger/Global/Zone/Login 的 IP:port |
| `LoggerServer/extern_logger.xml` | LoggerServer | listen 端口、logDir |
| `GlobalServer/extern_global.xml` | GlobalServer | listen、HTTP、可选 MySQL |
| `ZoneServer/extern_zone.xml` | ZoneServer | listen、可选 MySQL |
| `LoginServer/extern_login.xml` | LoginServer | ClientListen、RegisterListen、可选 MySQL |

`config/README.md` 也描述了 `config.xml` 与上述文件的关系。

---

## 3. 转发信封协议

| 消息 | 方向 | 说明 |
|------|------|------|
| SS_EXTERN_FWD_REQ | 区内 → Super | 信封 + inner module/sub/body |
| SS_EXTERN_FWD_RSP | Super → 区内 | 响应 |
| EXT_GAMEZONE_FWD_REQ | Super → 外联 | 同上 |
| EXT_GAMEZONE_FWD_RSP | 外联 → Super | 响应 |

实现：[`sdk/util/GameZoneExternSender.*`](../sdk/util/GameZoneExternSender.cpp)、[`SuperServer/SuperExternRouter.*`](../SuperServer/SuperExternRouter.cpp)、[`sdk/util/GameZoneMsgDispatch.*`](../sdk/util/GameZoneMsgDispatch.cpp)

### 典型用途

| 能力 | 路径 |
|------|------|
| 远程日志 | 区内 `RemoteLogClient` → SS_EXTERN_FWD → Logger `LOG_WRITE_REQ` |
| 全区排行 | Scene → SS_EXTERN_FWD → Global `GLB_RANK_UPDATE` |
| 充值/GM（骨架） | Session/Scene → SS_EXTERN_FWD → Login `LOGIN_RECHARGE/GM` |

---

## 4. LoginServer — 两阶段登录

### 4.1 流程

```mermaid
sequenceDiagram
    participant C as Client
    participant LS as LoginServer
    participant GW as Gateway
    participant SS as Super

    C->>LS: C2S_ZONE_LIST_REQ (9010)
    LS-->>C: S2C_ZONE_LIST_RSP
    C->>LS: C2S_LOGIN_REQ (9010)
    LS->>LS: MySQL CharBase.name 校验
    LS-->>C: S2C_LOGIN_RSP + S2C_GATEWAY_INFO
    C->>GW: 连接 gatewayIP:gatewayPort
    Note over GW,SS: 后续与区内直连登录相同
```

存量客户端可**跳过 LoginServer**，直连 Gateway（9005）。

### 4.2 双端口

| 端口 | 默认 | 监听方 | 用途 |
|------|------|--------|------|
| ClientListen | 9010 | LoginServer | 玩家区列表、登录、下发网关 |
| RegisterListen | 19010 | LoginServer | Super 代理的网关注册 |

### 4.3 Gateway 注册（经 Super 代理）

Gateway **不直连** Login RegisterListen，而是：

1. `SS_LOGIN_GATEWAY_WRAP_REQ` → Super → Login `LOGIN_GATEWAY_REGISTER_REQ`
2. 周期 `LOGIN_GATEWAY_HEARTBEAT`（经 Super 包装）

实现：[`SuperServer/SuperLoginMsg.*`](../SuperServer/SuperLoginMsg.cpp)、[`GatewayServer/GatewayServer.cpp`](../GatewayServer/GatewayServer.cpp)

### 4.4 区列表与 LoginAuthService

**配置命名区分**（勿与区内拓扑混淆）：

| 名称 | 位置 | 用途 |
|------|------|------|
| `serverlist.xml` | `LoginServer/serverlist.xml` | 玩家可见游戏区列表；启动加载至 `ZoneInfoStore` |
| `loginserverlist.xml` | 项目根 | 区内进程连外联 Logger/Global/Zone |
| MySQL `ServerList` | `tables/init.sql` | Super 区内拓扑 |
| MySQL `ZoneInfo` | `tables/init.sql` | 参考/种子数据；LoginServer **不再读取** |

- `extern_login.xml` 中 `<ServerList path="..."/>` 指定区列表路径（默认 `LoginServer/serverlist.xml`）
- `C2S_ZONE_LIST_REQ` / `S2C_ZONE_LIST_RSP`：登录前返回全部区服（含维护中条目；`loadLevel` 0畅通/1繁忙/2爆满/3维护）
- Super 每 15s 经 `LOGIN_ZONE_STATUS_REPORT` 上报区在线人数（Gateway 心跳汇总）与存活网关数；Login `ZoneInfoStore` 合并静态 `serverlist.xml` 与运行时数据
- `config.xml` `<Zone zoneId gameType/>` 与 `serverlist.xml` 区号须一致
- 登录 `C2S_LOGIN_REQ` 须带所选 `zoneId`/`gameType`；`S2C_GATEWAY_INFO` 按区从 `LoginGatewayRegistry` 轮询网关
- 可选 MySQL：同 Record 按 `CharBase.name` 查找/创建
- `LoginGatewayRegistry`：内存网关表，round-robin LB
- 10s 清理 stale 网关

### 4.5 骨架能力

- `LOGIN_RECHARGE_REQ` — 充值
- `LOGIN_GM_CMD_REQ` — GM 指令

经 `EXT_GAMEZONE_FWD` 解包后由 `LoginRechargeService` / `LoginGmService` 处理（当前为骨架）。

---

## 5. LoggerServer

| 项 | 说明 |
|----|------|
| 启动 | `./RunServer.sh logger` 或 `ENABLE_*` 等价 |
| 入站 | Super ExternalServerHub 连接 |
| 处理 | `LOG_WRITE_REQ` → `{logDir}/{SubServerType}.log` + 小时归档 |
| 配置 | `extern_logger.xml` |

---

## 6. GlobalServer

| 项 | 说明 |
|----|------|
| 启动 | `ENABLE_GLOBAL=1 ./RunServer.sh` |
| TCP | Super 连接；`GLB_RANK_UPDATE` / `GLB_DATA_SYNC` |
| HTTP | `GlobalHttpServer` — `/health`、`/rank`、`/getUserList` 等 |
| MySQL | 可选，HTTP getUserList 依赖 |
| 局限 | `SyncGlobalData()` 未向 Scene 推送 rank；`OnDataSync` fan-out 可用 |

**生产路径**：Super `ExternalServerHub`，非 Scene 直连 Global。

---

## 7. ZoneServer

| 项 | 说明 |
|----|------|
| 启动 | `ENABLE_ZONE=1 ./RunServer.sh` |
| 用途 | 跨区 `ZONE_CROSS_REQ` → `ZONE_FORWARD` |
| 状态 | **骨架**：`OnForward` 当前 log-only；路由表 `m_routes` 未完整登记 |

---

## 8. 启动命令

```bash
./RunServer.sh              # 仅区内 6 服
./RunServer.sh logger       # LoggerServer
./RunServer.sh global       # GlobalServer
./RunServer.sh zone         # ZoneServer
./RunServer.sh login        # LoginServer

# 或环境变量
ENABLE_GLOBAL=1 ENABLE_ZONE=1 ./RunServer.sh
```

外联服需先配置 `loginserverlist.xml`（Super 侧）与各 `extern_*.xml`。

---

## 9. 与 MySQL 的关系

| 进程 | MySQL |
|------|-------|
| SuperServer | 启动只读 `ServerList`（区内拓扑） |
| RecordServer | 写 CharBase / Relation |
| LoginServer | 可选：CharBase 验证；区列表来自 `serverlist.xml` |
| GlobalServer | 可选：HTTP API |
| ZoneServer | 可选（配置预留） |
| LoggerServer | 无 |

外联服**不在** `ServerList` 表登记。
