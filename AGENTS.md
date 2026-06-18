# RPG Server — Agent 开发指南

面向 AI 助手与自动化工具的本仓库协作说明。人类开发者亦可作速查。

## 项目是什么

分布式 MMORPG 服务端：**10 个 C++ 可执行进程**（6 区内 + 4 外联可选）+ **SceneServer 内嵌 Lua**。  
文档：[docs/INDEX.md](docs/INDEX.md) · [README.md](./README.md) · [ARCHITECTURE.md](ARCHITECTURE.md)

## 必须遵守的 Cursor Rules

| 规则文件 | 内容 |
|----------|------|
| `.cursor/rules/project.mdc` | 架构红线、目录约定、**6 字节消息头**、网关必校验 |
| `.cursor/rules/naming-conventions.mdc` | PascalCase / camelCase / ALL_CAPS |
| `.cursor/rules/comments-required.mdc` | **所有 `.h` / `config/*.xml` / `tables/*.sql`** 及新增源码的注释要求 |
| `docs/COMMENTS.md` | 注释规范人类可读版（与上表一致） |

以上规则均为 **alwaysApply**，修改代码时默认生效。

## 头文件注释（必遵）

- **新建 `.h`**：必须有文件头 `@file` + `@brief`；所有对外类型、public 方法、非自解释成员均须注释。
- **修改存量 `.h`**：本次新增的 class / struct / enum / 方法 / 成员变量 / 协议字段，**必须**按 [`docs/COMMENTS.md`](docs/COMMENTS.md) 同步补齐，不得只加裸声明。
- **排版**：相邻方法声明之间空一行（范本 `SceneServer/SceneUserManager.h`；注释风格参考 `LoginServer/LoginGatewayRegistry.h`）。

## 架构要点（不可违反）

- 单线程 `Poll()`，handler 内不阻塞、不加跨线程共享写
- 三库分工：**rpg_login**（LoginServer）、**rpg_game**（Super/Record/Session）、**rpg_global**（GlobalServer）
- RecordServer 为 **rpg_game** 角色数据主写库；Session 可直连 rpg_game 做本区玩法
- 全区场景/副本调度在 **SessionServer** `SessionSceneManager`
- 策划静态数据：**DataDoc Excel** → `./gen_data.sh` → **database/*.lua** → **basefile** 加载
- MySQL 表结构：**tables/**（入口 `init.sql`，与 `database/` Lua 配表平级）
- **客户端上行**必须经 Gateway `ClientMsgValidator` + `ClientMsgRouter`，禁止绕过

## 协议帧（客户端与服间共用）

```
| bodyLen (2B) | module (1B) | sub (1B) | body |
```

- 定义：`sdk/net/NetDefine.h`
- 工具：`sdk/net/MsgId.h`（`makeMsgId` / `msgModule` / `msgSub`）
- 客户端模块：`ClientModule` in `Common/ClientTypes.h`
- 新客户端消息须在 `ClientMsgValidator` 白名单与 `ClientMsgRouter` 中登记路由；协议体写入对应 `XxxCommon.h` / `XxxMsg.h`

## 常用路径

```
docs/INDEX.md                    # 文档总索引
sdk/net/NetDefine.h              # MsgHeader
sdk/net/MsgId.h                  # module/sub 工具
docs/PROTOCOL.md                 # 协议参考
docs/SERVERS.md                  # 10 进程说明
docs/COMMON.md                   # RPG_Common submodule 双端同步
Common/ClientTypes.h             # ClientModule（submodule）
Common/ClientMsgBody.h           # body 前缀与 initClientMsg
Common/LoginMsg.h 等 *Msg.h      # 各域 wire 消息
Common/*Common.h                  # 各域枚举/常量/辅助结构
protocal/InternalMsg.h           # Msg_GW_ClientMsg、Msg_GW_SendToClient
GatewayServer/ClientMsgValidator.h
GatewayServer/ClientMsgRouter.h
GatewayServer/GatewayServer.h
SceneServer/SceneServer.h
SessionServer/SessionServer.h
SessionServer/SessionSceneManager.*
script/scene/init.lua
docs/LUA.md                      # Lua 绑定与模块
```

## 提交前自检

- [ ] 三库配置与 `tables/init.sql` 一致（login/game/global）
- [ ] 未破坏单线程 / Session 调度边界 / 三库分工
- [ ] 新命名与注释符合 `.cursor/rules/*`（`.h` 文件头+API；XML 段/属性；SQL 表/字段 `COMMENT`）
- [ ] 新增/修改日志均为中文文案，且术语与项目约定一致（如“登录服/网关服/场景服/会话服/存档服/超级服/视野服/全局服/跨区服/日志服”）
- [ ] 新建或改动的 `.h` 中，**本次新增**符号均有 Doxygen 注释
- [ ] 新客户端消息：Validator 规则 + Router 目标 + `XxxMsgSub` + `XxxMsg.h`（含 body 前缀 module/sub、方向/触发/字段 `/**< */` 注释）
- [ ] 协议字段用 `WireStringUtil`；线上帧为 6 字节头（非旧 4 字节）
- [ ] 改策划表已跑 `./gen_data.sh`，未手改 `AUTO-GENERATED` 的 lua
- [ ] 未提交 `.build/`、`logs/`、`run/`、`.cache/`
- [ ] 未提交 `3Party/src/` 与 `3Party/lua|tinyxml2|mysql/` 编译产物；`3Party/vendor/*.tar.gz` 应已在库内

## 禁止

- Gateway/Scene/AOI 直连 MySQL（Session/Super/Record/Global/Login 按三库分工除外）
- 客户端消息绕过 Gateway 校验直转 Scene
- 在 Lua 或 C++ 中复制粘贴大段策划表（应走 DataDoc）
- 无说明地重命名存量 `OnXxx` / `m_` 前缀符号
- 使用 `strncpy` 写入 `ClientMsg` / `InternalMsg` 定长字符串字段
- 新增英文整句日志或在同一概念上混用中英文术语（如 `LoginServer` 与“登录服”并存）
