# 扩展开发指南

本文档汇总常见扩展任务的步骤与检查清单。协议细节见 [PROTOCOL.md](PROTOCOL.md)；各进程见 [SERVERS.md](SERVERS.md)。

---

## 1. 新增客户端消息

### 1.1 定义协议

客户端 wire 真源为 [`Common/*.proto`](../Common/)（RPG_Common 子模块）。**在 submodule 内 commit 并 push**，再在 RPG / RPG_Client 更新 submodule 指针。完整流程见 [COMMON.md](COMMON.md)。

1. 在 `XxxCommon.proto` 增加 `XxxMsgSub` 等 enum（行尾注释：方向、sub、处理方）
2. 在 `XxxMsg.proto` 定义 message（块注释：方向、module/sub、触发时机）
3. 运行 `./scripts/gen_proto.sh`（或 `./Build.sh` 自动执行）
4. 发送侧：[`ClientWireSend.h`](../sdk/net/ClientWireSend.h) 的 `sendClientProto` / `sendClientProtoModule`
5. 解析侧：[`ClientProtoWire.h`](../sdk/net/ClientProtoWire.h) 的 `parseProto`

对方拉取更新：

```bash
./pull.sh
# 或：git pull --recurse-submodules
```

### 1.2 网关登记（必须，且仅在 handler 落地后）

**勿**为未实现消息提前加入 Validator；否则客户端包会通过校验却无 handler。

[`GatewayServer/ClientMsgValidator.h`](../GatewayServer/ClientMsgValidator.h)：

- 白名单：允许的状态（未登录/已登录）
- 包长：`minLen=1`、`maxLen=CLIENT_PROTO_MAX_BODY`（Protobuf body）
- payload：**仅** `parseProto` + 业务约束；**禁止** body 内 module/sub 前缀或定长 struct
- legacy：命中 wire v2 定长包时返回 `BAD_PAYLOAD`（见 [PROTOCOL.md](PROTOCOL.md) §2.4）

[`GatewayServer/ClientMsgRouter.h`](../GatewayServer/ClientMsgRouter.h)：

- `LOCAL` / `SCENE` / `SESSION`

### 1.3 业务处理

| 路由目标 | 处理位置 |
|----------|----------|
| SCENE | `SceneClientMsgRegister` → `ClientMsgDispatcher` → Scene handler |
| SESSION | **当前未路由**（`ClientMsgRouter` 不返回 SESSION；见 [SERVERS.md](SERVERS.md) § SessionServer） |
| LOCAL | `LoginClientMsgRegister` 或 Gateway 本地 handler |

各服 `OnMessage` 统一 `MsgIngress::dispatchInternal`；Gateway 客户端连接：`handleClientMsg` → Validator → Router。

Scene Lua 扩展：见 [LUA.md](LUA.md) § 扩展指南。

### 1.4 自检

- [ ] `XxxMsg.proto` message + Scene/Session/Gateway handler **已实现**
- [ ] `./scripts/gen_proto.sh` 已运行且 `Protobuf/` 已更新
- [ ] Validator 白名单 + Router 目标（与 handler 同步添加）
- [ ] Unity `RpgMessageDispatcher` 注册 S2C handler（若客户端需收包）

---

## 2. 新增 S2S 消息

1. [`protocal/InternalMsg.h`](../protocal/InternalMsg.h) — `InternalMsgID` + struct
2. 发送方构造消息，`TcpClient::SendMsg` 或 `TcpServer::SendMsg`
3. 接收方 `*InternMsgRegister` — `registerInternal` / `registerInternalSized` / `registerInternalRaw`（见 `sdk/util/MsgHandlerBinder.h`）
4. 注释：方向、触发时机、payload 布局

---

## 3. 新增 CopyType（副本）

1. [`SceneServer/CopyScene.h`](../SceneServer/CopyScene.h) — 子类（如 `TeamCopyScene`）
2. [`CopySceneFactory`](../SceneServer/CopyScene.cpp) — 工厂分支
3. [`SessionServer/SessionCopyScene.h`](../SessionServer/SessionCopyScene.h) — 复用条件（若需要）
4. 调用 `SceneServer::requestCreateCopy(copyType, mapId, ownerId, ...)`

流程：Scene → `SES_COPY_CREATE_REQ` → Session 查复用或 `pickSceneServerId()` → `SES_COPY_CREATE_CMD`。

---

## 4. 水平扩展 SceneServer

1. 新进程设置环境变量 `RPG_SERVER_ID`（或与 `server_info.xml` 的 `sceneID` 一致）
2. 编辑 [`config/server_info.xml`](../config/server_info.xml) — 该实例承载的 `<Map>` 列表
3. MySQL `ServerList` 增加 Scene 行（或通过 Super 注册流程）
4. Gateway `GatewayScenePool` 自动连接全部 Scene 条目
5. Super 登录选服：当前为**首个存活 Scene**（非按 map 路由）

---

## 5. 水平扩展 GatewayServer

1. 新 Gateway 实例 + 不同 clientPort
2. L4 负载均衡（客户端连 VIP）
3. 各 Gateway 向 Super 注册，`SS_LOGIN_GATEWAY_WRAP` 向 Login 上报（若启用两阶段登录）
4. LoginServer `LoginGatewayRegistry` round-robin 选网关

---

## 6. 新增策划表

1. 在 `DataDoc/` 添加或修改 `.xlsx`（或 `./gen_data.sh --init`）
2. `./gen_data.sh` → `database/your_config.lua`
3. Lua：`DataTable.load("your_config")`
4. **勿手改** AUTO-GENERATED 的 lua 文件

详见 [DataDoc/README.md](../DataDoc/README.md)、[DATA.md](DATA.md)。

---

## 7. 新增 MySQL 表

1. 在 `tables/` 编写 SQL（表头 `--` 注释 + 字段 `COMMENT`）
2. 合并到 [`tables/init.sql`](../tables/init.sql)（`IF NOT EXISTS`）
3. 更新 [`tables/README.md`](../tables/README.md)、[DATA.md](DATA.md)
4. **仅 RecordServer** 实现读写（除非架构改造）
5. Session 扩展数据经 `REC_RELATION_*` 或 CharBase.binary

---

## 8. 构建与运行

```bash
./autoinit.sh              # 首次：从 vendor 离线编译 3Party + cmake（无需 curl）
./gen_data.sh              # 改 Excel 后
./pull.sh                  # 日常：拉取主仓库 + Common 子模块
./Build.sh                 # 编译全部 10 个服务器
./Build.sh LoginServer     # 或只编译指定服（支持多个目标）
./Build.sh clean           # 清除 .build/ 与各服目录下可执行文件
./Build.sh rebuild         # clean 后全量重编
mysql -u root -p < tables/init.sql
./RunServer.sh             # 从项目根启动（影响 Lua 路径）
./log.sh                   # 跟踪 logs/*.log
./StopServer.sh
```

产物：各服目录下可执行文件（如 `SuperServer/SuperServer`、`LoginServer/LoginServer`）。

### 8.1 升级 3Party 版本（维护者）

1. 修改 [`3Party/versions.env`](../3Party/versions.env) 中的版本号与 URL
2. `./3Party/fetch_vendor.sh --force`（需 curl 与网络）
3. `git add 3Party/vendor/` 并提交
4. 团队 `git pull` 后执行 `./3Party/download_and_build.sh --force`

详见 [3Party/README.md](../3Party/README.md)。

---

## 9. 调试技巧

| 场景 | 方法 |
|------|------|
| 协议排查 | 查 flat ID：`makeMsgId(module, sub)`；对照 [PROTOCOL.md](PROTOCOL.md) |
| 网关拒绝 | 客户端收 `S2C_ERROR`（0x0F/0x05），看 `GatewayValidateCode` |
| 登录失败 | 顺序查 Gateway → Record → Super → Scene 日志 |
| 脚本错误 | `logs/scene.log` 中 `[脚本]` 前缀 |
| 外联不通 | 查 `loginserverlist.xml`、Super 日志、`SS_EXTERN_FWD` |

---

## 10. 架构红线（禁止）

- Handler 内阻塞 IO、长锁、跨线程写共享状态
- Gateway/Scene/Session 直连 MySQL（Record 唯一写库入口）
- 客户端消息绕过 Gateway Validator/Router
- Lua/C++ 硬编码大段策划表（应走 DataDoc）
- `strncpy` 写协议定长字符串字段

完整规则：[AGENTS.md](../AGENTS.md)、[`.cursor/rules/project.mdc`](../.cursor/rules/project.mdc)
