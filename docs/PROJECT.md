# RPG Server — 项目说明与项目总结

本文档面向新成员与协作方，说明项目定位、架构与现状；技术细节见 [ARCHITECTURE.md](ARCHITECTURE.md)，上手步骤见 [README.md](../README.md)。

---

## 一、项目说明

### 1.1 项目定位

**RPG Server** 是一套运行在 Linux 上的**分布式 MMORPG 游戏服务器**参考实现。

- **核心逻辑**：C++17（网络、调度、场景实例、存档）
- **玩法脚本**：Lua 5.4（SceneServer 内嵌 VM）
- **协作方式**：多进程 + TCP 长连接 + 自定义二进制协议

目标是在可水平扩展的前提下，将登录、存档、视野、场景、网关等能力解耦到独立服务，便于学习大型网游服务端架构或在此基础上扩展玩法与运维能力。

### 1.2 技术栈

| 类别 | 技术 |
|------|------|
| 语言 | C++17、Lua 5.4 |
| 网络 | 单线程 epoll ET + 自研 TCP（`sdk/net`） |
| 配置 | XML（tinyxml2） |
| 持久化 | MySQL（MariaDB Connector/C，仅 RecordServer 直连） |
| 策划数据 | Excel（`DataDoc/`）→ Lua 配表（`database/`） |
| 构建 | CMake 3.16+，产物默认输出到各服务器目录（如 `SuperServer/SuperServer`） |
| 第三方库 | Lua、tinyxml2、MariaDB 客户端（`3Party/` 自包含） |

### 1.3 系统架构（9 进程）

```
Client → GatewayServer ─┬→ SceneServer / AOIServer
         │ 校验+路由      └→ SessionServer（社交/任务）
         ↓
    SuperServer → RecordServer → MySQL
         ↑
    SessionServer（全区场景/副本调度）
```

| 服务器 | 默认端口 | 职责概要 |
|--------|----------|----------|
| SuperServer | 9000 | 注册中心、登录调度、用户路由 |
| SessionServer | 9001 | 社会关系、离线数据、全区场景/副本登记与负载均衡 |
| RecordServer | 9002 | 唯一连库进程，账号验证、读写存档 |
| AOIServer | 9003 | 9 宫格 AOI 视野 |
| SceneServer | 9004 | 在线玩法、地图实例、Lua 脚本（可多台水平扩展） |
| GatewayServer | 9005 / 19005 | 客户端接入、消息校验、按 module 转发 Scene/Session |
| LoggerServer | 9006 | 集中写日志 |
| GlobalServer | 9007 | 全区数据（可选，`ENABLE_GLOBAL=1`） |
| ZoneServer | 9008 | 跨区转发（可选，`ENABLE_ZONE=1`） |

**设计原则**：

- 单线程无锁：每进程一个 `Poll()` 事件循环 + `TimerMgr`
- SuperServer 中心化：子服注册、心跳、按用户/地图路由
- DB 收敛：仅 RecordServer 访问 MySQL
- Scene 可扩展：多台 SceneServer，Session 侧统一登记与选服

### 1.4 目录结构

```
RPG/
├── sdk/              # 网络、日志、定时器、配置、消息分发
├── common/           # 客户端协议 ClientMsg.h
├── protocal/         # 服间协议 InternalMsg.h
├── SuperServer/      # 各进程实现（每服一目录）
├── SessionServer/
├── RecordServer/
├── AOIServer/
├── SceneServer/
├── GatewayServer/
├── LoggerServer/
├── GlobalServer/
├── ZoneServer/
├── config/           # config.xml、server_info.xml
├── DataDoc/          # 策划 Excel 源表
├── database/         # 生成的 *_config.lua 策划配表
├── tables/           # MySQL DDL（入口 init.sql）
├── basefile/         # 配表加载工具 data_table.lua
├── script/           # 游戏 Lua（scene/quest/npc 等）
├── 3Party/           # 第三方静态库
├── tools/            # Excel→Lua 生成（gen_datadoc.py）
├── docs/             # 架构与项目文档
├── Build.sh          # 编译脚本
├── autoinit.sh       # 环境初始化
├── gen_data.sh       # 配表生成
├── RunServer.sh      # 启动集群
└── StopServer.sh     # 停止集群
```

### 1.5 核心子系统

#### 网络与消息

- 帧格式：`MsgHeader { bodyLen, module, sub }`（6 字节）+ body，见 `sdk/net/NetDefine.h`
- 工具：`sdk/net/MsgId.h`（`makeMsgId` / `msgModule` / `msgSub`）
- 分发：`MsgDispatcher` 按 `(module, sub)` 查表；仍支持扁平 `uint16_t` 注册
- 协议：`common/ClientMsg.h`（客户端）、`protocal/InternalMsg.h`（服间）
- 网关：`ClientMsgValidator` + `ClientMsgRouter`（`GatewayServer/`）

#### Gateway 客户端消息

1. 拆包 → 2. 校验（白名单/长度/状态/userID）→ 3. 路由（Scene / Session / 本地）→ 4. 失败回 `S2C_ERROR`

#### 用户模型

```
UserBase → IUser → SessionUser / RecordUser / SceneUser
```

- RecordServer：账号、角色持久化（`tables/init.sql`）
- SceneServer：在线实体 `SceneUser`、`SceneNpc`（`SceneEntry`）

#### 场景与副本

| 进程 | 核心类 | 职责 |
|------|--------|------|
| SceneServer | `Scene` / `CopyScene` | 本进程场景实例运行 |
| SceneServer | `SceneManager` | 管理普通图与副本 |
| SceneServer | `CopySceneFactory` | 按 `CopyType` 创建副本子类 |
| SessionServer | `SessionScene` | 普通地图全区注册记录 |
| SessionServer | `SessionCopyScene` | 副本实例记录（含复用判断） |
| SessionServer | `SessionSceneManager` | 全区场景/副本表 + SceneServer 负载均衡 |

**流程要点**：

1. SceneServer 启动时按 `server_info.xml` 创建普通 `Scene`，成功后向 AOI、Session 注册
2. 副本创建：Scene 发 `SES_COPY_CREATE_REQ` → Session 查复用或 `pickSceneServerId()` 选服 → 目标 Scene 执行 `SES_COPY_CREATE_CMD`
3. 普通场景实例 ID：`(sceneServerId << 32) | mapId`

#### Lua 与策划数据

- SceneServer：`LuaManager` 加载 `script/scene/init.lua`，C++ 调用 `OnTick`、`OnUserEnter` 等
- 配表：`DataDoc/*.xlsx` → `./gen_data.sh` → `database/*_config.lua` → `DataTable.load()`（`basefile/data_table.lua`）
- 模块：事件系统、NPC 管理、技能、任务（`script/scene`、`script/quest`）

#### 登录主路径

```
Client → Gateway（验证）→ Super → Record（加载）→ Scene → AOI
         ← Gateway ← Super ← Scene（进入完成）
```

### 1.6 开发与运维流程

```bash
./autoinit.sh          # 3Party + CMake 配置
./gen_data.sh          # Excel → Lua 配表（首次可加 --init）
./Build.sh             # 编译全部服务器
mysql -u root -p < tables/init.sql          # 建库建表
mysql -u root -p < tables/seed_test_data.sql  # 可选：开发测试账号
./RunServer.sh         # 按依赖顺序启动
./log.sh               # 实时日志
./StopServer.sh        # 停止
```

测试账号（执行 `seed_test_data.sql` 后可用）：`test001` / `123456`

### 1.7 开发规范

| 规范 | 说明 |
|------|------|
| 类名 | PascalCase（`UserManager`） |
| 变量/函数 | camelCase（`userCount`、`getUserName()`） |
| 宏/常量 | ALL_CAPS（`MAX_NUM`） |
| 协议字符串 | 使用 `sdk/util/WireStringUtil.h`，禁止裸 `strncpy` 写 wire 字段 |
| 注释 | 新文件、协议、配置需有必要注释（见 `.cursor/rules/`） |

### 1.8 推荐阅读顺序

1. `sdk/net/NetDefine.h` + `sdk/net/MsgId.h` — 6 字节消息帧
2. `common/ClientMsg.h` — 客户端 module/sub 与结构体
3. `GatewayServer/ClientMsgValidator.h` + `ClientMsgRouter.h` — 网关校验与路由
4. `protocal/InternalMsg.h` — 服间协议与 `Msg_GW_*` 转发
5. `GatewayServer/GatewayServer.h` — 接入与登录
6. `SuperServer/SuperServer.h` — 注册与调度
7. `SceneServer/SceneServer.h` — 核心玩法
8. `SessionServer/SessionSceneManager.h` — 全区场景/副本调度

---

## 二、项目总结

### 2.1 已完成能力

| 维度 | 说明 |
|------|------|
| 分布式骨架 | 9 服拆分、注册心跳、统一启动/停止脚本 |
| 登录与存档 | Gateway + Super + Record 链路，MySQL 表结构完整 |
| 场景运行 | Scene/CopyScene、SceneManager、地图配置、AOI 登记 |
| 全区调度 | SessionSceneManager：注册、副本复用、SceneServer 负载选择 |
| 客户端接入 | Gateway 双端口、6 字节消息头、校验与按模块转发 Scene/Session |
| 脚本层 | Lua VM、事件/NPC/技能/任务框架、C++↔Lua 绑定 |
| 策划数据 | DataDoc → database Lua + basefile 加载，集成 autoinit/build |
| 工程化 | CMake、3Party 自包含、架构文档与 README |

当前状态：**可编译、可启动、可扩展的多进程 MMORPG 服务端框架**，而非单进程 Demo。

### 2.2 架构亮点

1. **职责清晰**：DB、AOI、日志、网关、玩法分层，符合常见中大型网游拆分方式
2. **Scene 水平扩展**：Session 维护全区视图，`pickSceneServerId()` 按场景数与玩家数加权选服
3. **副本复用**：相同 `copyType + mapId + ownerId` 可复用未满员实例，减少重复创建
4. **C++ / Lua 分工**：网络、调度、实例生命周期在 C++；任务、NPC、事件在 Lua
5. **双轨数据**：MySQL 管玩家存档；Excel/Lua 管静态策划表，边界明确
6. **网关安全边界**：客户端上行统一校验与路由，玩法服不直接面对未校验流量

### 2.3 关键代码锚点

| 模块 | 路径 |
|------|------|
| 消息帧 | `sdk/net/NetDefine.h`、`sdk/net/MsgId.h` |
| 网关校验/路由 | `GatewayServer/ClientMsgValidator.h`、`ClientMsgRouter.h` |
| 全区场景管理 | `SessionServer/SessionSceneManager.cpp` |
| 本进程场景 | `SceneServer/SceneManager.h` |
| Lua 入口 | `script/scene/init.lua`、`SceneServer/LuaManager.cpp` |
| 配表加载 | `basefile/data_table.lua` |
| 协议定义 | `protocal/InternalMsg.h`、`common/ClientMsg.h` |

### 2.4 局限与后续方向

| 领域 | 现状 | 可扩展方向 |
|------|------|------------|
| 地图资源 | `.map` 配置为主，解析较简 | 碰撞、传送点、完整资源加载 |
| 战斗/技能 | 框架已有，逻辑较简 | 技能表、伤害公式、Buff 状态机 |
| Global/Zone | 可选进程，业务较少 | 排行榜、跨服、合区 |
| 热更新 | 需重启或手动重载 Lua | 配表/Lua 热更流程 |
| 监控运维 | 以文件日志为主 | 指标、链路追踪 |
| 自动化测试 | 手工启动为主 | 协议单测、压测 |

### 2.5 文档索引

| 文档 | 内容 |
|------|------|
| [AGENTS.md](../AGENTS.md) | AI/协作开发指南与自检清单 |
| [.cursor/rules/project.mdc](../.cursor/rules/project.mdc) | Cursor 项目总则（架构红线） |
| [README.md](../README.md) | 快速上手、场景流程、日志 |
| [ARCHITECTURE.md](ARCHITECTURE.md) | 架构图、协议、扩展指南 |
| [DataDoc/README.md](../DataDoc/README.md) | Excel 配表规范 |
| [database/README.md](../database/README.md) | Lua 策划配表说明 |
| [tables/README.md](../tables/README.md) | MySQL 表结构脚本 |
| [COMMENTS.md](COMMENTS.md) | 头文件 / XML / SQL / 源码注释规范 |
| [3Party/README.md](../3Party/README.md) | 第三方库构建 |

### 2.6 一句话总结

**RPG Server** 是以 SuperServer 为枢纽、Record 存库、Session 调度全区场景与副本、Scene+Lua 承载玩法、Gateway 对接客户端的分布式 MMORPG 服务端项目；在工程结构、协议分层、场景扩展与策划配表管线上已成型，适合继续完善玩法、地图资源与生产运维能力。

---

*Internal project — 详见仓库 License 说明。*
