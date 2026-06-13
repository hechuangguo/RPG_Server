# RPG Server 文档索引

本文档为仓库**总导航**。快速上手见 [README.md](../README.md)；架构概览见 [ARCHITECTURE.md](ARCHITECTURE.md)。

---

## 按角色阅读

### 新人（服务端）

1. [README.md](../README.md) — 环境、编译、启动
2. [PROJECT.md](PROJECT.md) — 项目定位与技术栈
3. [ARCHITECTURE.md](ARCHITECTURE.md) — 进程拓扑与核心流程
4. [SDK.md](SDK.md) — 网络、定时器、配置、Bootstrap
5. [PROTOCOL.md](PROTOCOL.md) — 客户端与服间协议
6. [SERVERS.md](SERVERS.md) — 10 个进程职责与 handler
7. [SceneServer/SceneServer.h](../SceneServer/SceneServer.h) — 核心玩法入口

### 客户端 / 协议对接

1. [PROTOCOL.md](PROTOCOL.md) — 6 字节帧、ClientModule、消息表
2. [common/ClientMsg.h](../common/ClientMsg.h) — 结构体定义
3. [GatewayServer/ClientMsgValidator.h](../GatewayServer/ClientMsgValidator.h) — 校验规则
4. [GatewayServer/ClientMsgRouter.h](../GatewayServer/ClientMsgRouter.h) — 路由目标

### Lua / 玩法

1. [LUA.md](LUA.md) — C++↔Lua API、模块说明
2. [script/README.md](../script/README.md) — 脚本目录与扩展步骤
3. [basefile/README.md](../basefile/README.md) — `DataTable` 配表 API
4. [DataDoc/README.md](../DataDoc/README.md) — Excel 策划表规范

### 运维 / DBA

1. [README.md](../README.md) § 数据库与启动
2. [tables/README.md](../tables/README.md) — MySQL 脚本
3. [DATA.md](DATA.md) — 表结构与读写进程
4. [config/README.md](../config/README.md) — 运行时 XML 配置
5. [EXTERNAL.md](EXTERNAL.md) — 外联四服与 `loginserverlist.xml`

### AI / 自动化协作

1. [AGENTS.md](../AGENTS.md) — 架构红线与提交自检
2. [COMMENTS.md](COMMENTS.md) — 注释规范
3. [.cursor/rules/project.mdc](../.cursor/rules/project.mdc) — Cursor 总则

---

## 文档清单

| 文档 | 内容 |
|------|------|
| [README.md](../README.md) | 快速上手、场景/登录概要、时间库、日志 |
| [PROJECT.md](PROJECT.md) | 项目说明、技术栈、已完成能力与局限 |
| [ARCHITECTURE.md](ARCHITECTURE.md) | 架构图、启动顺序、各服概要、分层 |
| [SDK.md](SDK.md) | `sdk/` 全模块说明 |
| [PROTOCOL.md](PROTOCOL.md) | 客户端与服间协议参考 |
| [SERVERS.md](SERVERS.md) | 10 进程详细说明（连接、handler、定时器） |
| [EXTERNAL.md](EXTERNAL.md) | Logger/Global/Zone/Login 外联架构 |
| [DATA.md](DATA.md) | MySQL + 策划 Lua 双轨数据 |
| [LUA.md](LUA.md) | SceneServer Lua 脚本体系 |
| [DEVELOPMENT.md](DEVELOPMENT.md) | 扩展开发指南（消息、副本、配表、构建） |
| [COMMENTS.md](COMMENTS.md) | 头文件 / XML / SQL 注释规范 |

### 子目录 README

| 路径 | 内容 |
|------|------|
| [sdk/README.md](../sdk/README.md) | SDK 短索引 |
| [script/README.md](../script/README.md) | Lua 模块树 |
| [basefile/README.md](../basefile/README.md) | 配表加载 API |
| [config/README.md](../config/README.md) | `config.xml` / `server_info.xml` |
| [database/README.md](../database/README.md) | 生成的 Lua 策划配表 |
| [tables/README.md](../tables/README.md) | MySQL DDL 与初始化 |
| [DataDoc/README.md](../DataDoc/README.md) | Excel 源表规范 |
| [3Party/README.md](../3Party/README.md) | 第三方库构建 |

---

## 进程一览

**区内核心（6，必选）**：Super → Record / AOI → Session → Scene → Gateway  
**外联可选（4）**：Logger、Global、Zone、Login（`RunServer.sh` 按需）

详见 [SERVERS.md](SERVERS.md) 与 [EXTERNAL.md](EXTERNAL.md)。

---

## 源码锚点速查

| 主题 | 路径 |
|------|------|
| 消息帧 | `sdk/net/NetDefine.h`、`sdk/net/MsgId.h` |
| 客户端协议 | `common/ClientMsg.h` |
| 服间协议 | `protocal/InternalMsg.h` |
| 网关校验/路由 | `GatewayServer/ClientMsgValidator.h`、`ClientMsgRouter.h` |
| 全区场景调度 | `SessionServer/SessionSceneManager.*` |
| Lua 入口 | `script/scene/init.lua`、`SceneServer/LuaManager.*` |
| 配表加载 | `basefile/data_table.lua` |
| MySQL DDL | `tables/init.sql` |
