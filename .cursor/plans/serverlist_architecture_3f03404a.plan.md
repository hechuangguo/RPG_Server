---
name: serverlist architecture
overview: 新增 DB 表 ServerList（编号/类型/ip/端口/名称），SuperServer 启动直连 MySQL 加载 ServerList 并对外提供查询；Record/Session/AOI/Scene/Gateway 启动时先向 SuperServer 拉取 ServerList，据此绑定自身端口、连接对端并注册，替代 config.xml 端口段与写死的 127.0.0.1。
todos:
  - id: db-serverlist
    content: init.sql 新增 ServerList 表 + 9 行拓扑数据（注释齐全）
    status: completed
  - id: proto-serverlist
    content: InternalMsg.h：Register 加 name，新增 S2S_SERVERLIST_REQ/RSP 与 ServerEntry/Req 结构
    status: completed
  - id: sdk-serverlist
    content: 新增 sdk/util/ServerList.h/.cpp：ServerList 容器 + ServerListClient::fetch 同步拉取
    status: completed
  - id: super-serverlist
    content: SuperServer 启动连库加载 ServerList、响应查询、SubServerInfo 加 name，main.cpp 传 cfg
    status: completed
  - id: children-serverlist
    content: Record/Session/AOI/Scene/Gateway 启动改为从 ServerList 取自身端口/对端并注册(带 name)
    status: completed
  - id: config-trim
    content: config.xml/ConfigLoader 移除 5 个在册服务器端口段并更新注释
    status: completed
  - id: build-verify
    content: Build.sh clean 编译、执行 init.sql、启停抽验、修复改动文件 lints
    status: completed
isProject: false
---

# 服务器架构优化：ServerList 驱动注册

## 目标与已确认决策

- SuperServer 启动**直连 MySQL 只读** `ServerList`（为启动拓扑放宽“仅 Record 连库”红线）。
- `ServerList` **取代 config.xml 端口段**：在册服务器只从 config 读 SuperServer 地址（+ 自身 serverID），其余 ip/port/peer 全来自 ServerList。
- 子服不能读库，故由 **SuperServer 下发** ServerList：子服启动先连 Super 拉取，再绑定/连对端/注册。

## 作用域（明确，便于确认）

- 纳入新流程：`RecordServer`、`SessionServer`、`AOIServer`、`SceneServer`、`GatewayServer`（用户点 2-6）+ `SuperServer`。
- **不改**：`LoggerServer`、`GlobalServer`、`ZoneServer` 维持现状（仍读 config.xml 端口），其 config 端口项保留。
- serverID 来源：每个二进制的 `SubServerType` 编译期已知；serverID 默认 `1`，可用环境变量 `RPG_SERVER_ID` 覆盖（多实例预留），不引入 per-process 配置文件。

## 启动时序（新）

```mermaid
sequenceDiagram
    participant DB as MySQL
    participant Super as SuperServer
    participant Child as 子服(Record/Session/AOI/Scene/Gateway)
    Super->>DB: 连接并 SELECT ServerList
    Super->>Super: 缓存 ServerList, 监听 superPort
    Child->>Super: S2S_SERVERLIST_REQ(type,id) (临时连接)
    Super-->>Child: S2S_SERVERLIST_RSP(全量条目)
    Child->>Child: 取自身条目→bind 端口; 取对端条目→连接
    Child->>Super: S2S_REGISTER_REQ(type,id,ip,port,name)
    Super->>Super: 绑定 ConnID→ServerList 条目
```

## 1) 数据库 — [`tables/init.sql`](/home/hcg/RPG/tables/init.sql)

新增表（含注释，符合 SQL 注释规范）：

```sql
CREATE TABLE IF NOT EXISTS ServerList (
    server_id    INT UNSIGNED NOT NULL COMMENT '服务器实例编号',
    server_type  TINYINT UNSIGNED NOT NULL COMMENT '服务器类型(对应 SubServerType: 1 Session..8 Zone, 0 Super)',
    ip           VARCHAR(64) NOT NULL DEFAULT '127.0.0.1' COMMENT '监听IP',
    port         SMALLINT UNSIGNED NOT NULL COMMENT '监听端口',
    name         VARCHAR(32) NOT NULL DEFAULT '' COMMENT '服务器名',
    PRIMARY KEY (server_type, server_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
```

- 在 init.sql 内置 9 行 `INSERT`（Super 9000、Session 9001 … Zone 9008），与现有端口一致。
- [`tables/seed_test_data.sql`](/home/hcg/RPG/tables/seed_test_data.sql) 不放 ServerList（属拓扑而非测试数据，放 init.sql 保证可重复执行）。

## 2) 协议 — [`protocal/InternalMsg.h`](/home/hcg/RPG/protocal/InternalMsg.h)

- `Msg_S2S_Register` 增 `char name[32]`（点 8 的“服务器名”随注册上报）。
- 新增 ID（接 0x1F04 之后）：`S2S_SERVERLIST_REQ=0x1F05`、`S2S_SERVERLIST_RSP=0x1F06`。
- 新增结构（含方向/时机注释）：
  - `Msg_S2S_ServerListReq{ uint8_t serverType; uint32_t serverID; }`
  - `Msg_ServerEntry{ uint32_t serverID; uint8_t serverType; char ip[32]; uint16_t port; char name[32]; }`
  - 响应布局：`uint16_t count` + `count × Msg_ServerEntry`（变长，复用现有变长发送方式）。

## 3) SDK — 新增 [`sdk/util/ServerList.h`](/home/hcg/RPG/sdk/util/ServerList.h)（+ `.cpp`）

- `struct ServerEntry{ uint32_t id; SubServerType type; std::string ip; uint16_t port; std::string name; }`。
- `class ServerList`：`add()`、`find(type,id)`、`findFirst(type)`、`all()`，按 `type` 索引。
- `ServerListClient::fetch(superIP, superPort, myType, myId, ServerList& out, int timeoutMs)`：用临时 `TcpClient` 连 Super、发 `S2S_SERVERLIST_REQ`、`Poll` 至收到 `S2S_SERVERLIST_RSP` 或超时，解析填充。供 5 个子服启动调用。
- 声明在 .h、实现在 .cpp（沿用 `sdk/math` 已接入 CMake 的 `GLOB_RECURSE sdk/*.cpp`）。

## 4) SuperServer — [`SuperServer.h`](/home/hcg/RPG/SuperServer/SuperServer.h) / [`.cpp`](/home/hcg/RPG/SuperServer/SuperServer.cpp)

- `Init` 增参 `const ServerConfig& cfg`（取 DB 连接信息）；新增 `MYSQL* m_db` + `bool loadServerList()`（`SELECT server_id,server_type,ip,port,name FROM ServerList`）填充成员 `ServerList m_serverList`。
- 注册处理新增 `S2S_SERVERLIST_REQ` → 回 `S2S_SERVERLIST_RSP`（全量条目）。
- `SubServerInfo` 增 `std::string name`；`OnRegister` 记录 name 并可对照 `m_serverList` 校验。
- 析构关闭 `m_db`。[`SuperServer/main.cpp`](/home/hcg/RPG/SuperServer/main.cpp) 改 `Init(cfg.superIP, superPort, cfg)`。

## 5) 五个子服改造（Record/Session/AOI/Scene/Gateway）

各 `main.cpp`：读 cfg 后，先 `ServerListClient::fetch(...)` 取 `ServerList list`，再把 `list` 传入 `Init`。各 `Init`：

- 自身监听端口 = `list.find(myType, myId).port`（取代 `cfg.*Port`）。
- 连接对端 = `list.findFirst(peerType).ip/port`（取代写死 `127.0.0.1` + `cfg.*Port`）；Gateway 内网口仍按 `gatewayPort+10000` 约定，基于 ServerList 的 gateway 端口推导。
- `RegisterToSuper` 填 `serverType/serverID/ip/port/name`（name 来自自身条目）。
- Super 地址仍取 `cfg.superIP/superPort`（bootstrap）。

涉及：`RecordServer.cpp:107`、`SessionServer.cpp:162`、`AOIServer.cpp:184`、`SceneServer.cpp:25/666`、`GatewayServer.cpp:22/327` 等 `Connect/RegisterToSuper` 处。

## 6) 配置 — [`config/config.xml`](/home/hcg/RPG/config/config.xml) + [`ConfigLoader.h`](/home/hcg/RPG/sdk/util/ConfigLoader.h)

- 移除 5 个在册服务器的端口节点（Session/Record/AOI/Scene/Gateway）及 `ConfigLoader` 中对应 `loadPort`（改由 ServerList 提供）。
- 保留 `Database`、`SuperServer(ip,port)`、`LoggerServer/GlobalServer/ZoneServer` 端口、`LogPaths`。
- config.xml 文件头注释更新说明：端口已迁移至 DB 的 ServerList，子服启动经 SuperServer 下发。

## 7) 构建与验证

- `./Build.sh clean && ./Build.sh`（重 glob 新 sdk/*.cpp）。
- DB 执行更新后的 init.sql（建 ServerList 并插入 9 行）。
- 启停抽验：`./RunServer.sh` → 观察 Super 加载 ServerList、子服拉取并注册（带 name）→ `./StopServer.sh`。
- 读 lints，修复改动文件本次告警（既有 clangd 路径告警不处理）。

## 注意点 / 取舍

- 放宽红线：仅 Super 启动期**只读** ServerList；写库仍只在 Record。文档（AGENTS.md/project.mdc）如需同步可后续单独提。
- 启动依赖变强：子服需 Super 先就绪且 DB 可达；`fetch` 超时需有清晰报错与退出码。
- Logger/Global/Zone 暂不纳入，保持 config 端口，避免越界扩散。

## 验收标准

- `ServerList` 表存在且 init.sql 可重复执行；含 9 行拓扑。
- SuperServer 启动加载 ServerList 并能响应查询；`SubServerInfo` 带 name。
- 5 个子服启动从 ServerList 取端口/对端并注册（携带 name），不再依赖 config 端口段与写死 127.0.0.1。
- 全部编译通过，启停流程跑通。