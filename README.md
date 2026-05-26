# RPG Server

Linux 平台下的分布式 MMORPG 游戏服务器，采用 **C++17 + Lua 5.4**，共 **9 个独立进程** 组成微服务集群。

## 架构概览

```
Client → GatewayServer ─┬→ SceneServer → AOIServer
         │ 校验+按模块路由  └→ SessionServer（社交/任务）
         ↓
    SuperServer → RecordServer → MySQL
         ↑
    全区场景注册 · 副本调度 · 负载均衡（SessionSceneManager）
```

| 服务器 | 端口 | 职责 |
|--------|------|------|
| SuperServer | 9000 | 注册中心、登录调度 |
| SessionServer | 9001 | 社会关系、离线数据、**全区场景/副本调度** |
| RecordServer | 9002 | MySQL 持久化 |
| AOIServer | 9003 | 9 宫格视野管理 |
| SceneServer | 9004 | 在线逻辑、Lua 脚本、**Scene / CopyScene 实例** |
| GatewayServer | 9005 / 19005 | 客户端接入、**消息校验**、按模块转发 Scene/Session |
| LoggerServer | 9006 | 集中日志 |
| GlobalServer | 9007 | 全区数据（可选） |
| ZoneServer | 9008 | 跨区转发（可选） |

完整架构说明见 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)。  
项目说明与总结见 [docs/PROJECT.md](docs/PROJECT.md)。

## 协议帧格式

客户端与服间 TCP **共用**同一帧结构（定义于 [`sdk/net/NetDefine.h`](sdk/net/NetDefine.h)）：

| 字段 | 长度 | 说明 |
|------|------|------|
| bodyLen | 2 | 消息体字节数（不含 6 字节头） |
| module | 1 | 功能模块号（见 `ClientModule`） |
| sub | 1 | 模块内子消息号 |
| body | 变长 | `Msg_C2S_*` / `Msg_*` 等结构体 |

扁平协议号（查表、日志）：`makeMsgId(module, sub) == (module << 8) | sub`，工具见 [`sdk/net/MsgId.h`](sdk/net/MsgId.h)。

**示例**（登录请求 `C2S_LOGIN_REQ`，module=0x00, sub=0x01）：

```
[bodyLen=64][0x00][0x01][Msg_C2S_LoginReq 64B]
```

### Gateway 上行处理

| 步骤 | 组件 | 说明 |
|------|------|------|
| 1 | `TcpConnection` | 拆出 module/sub/body |
| 2 | `ClientMsgValidator` | 白名单、包长、登录状态、userID、基础字段 |
| 3 | `ClientMsgRouter` | 决定本地处理或转发 |
| 4 | 失败 | 回 `S2C_ERROR`（module=0x0F, sub=0x05） |

| module | 路由目标 |
|--------|----------|
| 0x00 Login、0x0F System | Gateway 本地（登录/心跳） |
| 0x01 Scene、0x02 Battle、0x04 Skill、0x08 NPC、0x05 Chat（广播） | SceneServer |
| 0x06 Social、0x07 Quest、0x05 Chat（sub=0x03 私聊） | SessionServer |

服间转发客户端包：`Msg_GW_ClientMsg`（含 `clientConnID` + module + sub + body）。下行：`Msg_GW_SendToClient`。

详见 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) 第 5、6 节。

## 开发约定

| 规范 | 说明 |
|------|------|
| **注释** | 新增代码、文件、配置必须附带必要注释 → [comments-required.mdc](.cursor/rules/comments-required.mdc) |
| **类名** | 大驼峰 PascalCase，如 `UserManager` |
| **变量 / 函数** | 小驼峰 camelCase，如 `userCount`、`getUserName()` |
| **宏 / 常量** | 全大写 + 下划线，如 `MAX_NUM` |
| **协议定长字符串** | 使用 `copyToWire` / `copyWireField`（[sdk/util/WireStringUtil.h](sdk/util/WireStringUtil.h)），**禁止**新增 `strncpy` 拷贝 wire 字段 |

命名细则 → [naming-conventions.mdc](.cursor/rules/naming-conventions.mdc)  
AI/协作总则 → [AGENTS.md](AGENTS.md) · [project.mdc](.cursor/rules/project.mdc)

协议定长字符串（`InternalMsg.h` / `ClientMsg.h` 中 `char[N]` 字段）统一用 [WireStringUtil.h](sdk/util/WireStringUtil.h)：

- `copyToWire(dst, dstSize, src)`：`std::string` 或 C 字符串 → 协议 `char[N]` 字段
- `copyWireField(dst, src)` 或 `copyWireField(dst, dstSize, src, srcSize)`：协议定长字段 → 协议定长字段（如 `req->mapName` → `rsp.mapName`）
- SQL、日志格式化仍用 `snprintf` / `vsnprintf`，不在此规则范围

```cpp
#include "sdk/util/WireStringUtil.h"

copyToWire(reg.ip, sizeof(reg.ip), "127.0.0.1");
copyToWire(rsp.mapName, sizeof(rsp.mapName), mapName.c_str());
copyWireField(rsp.mapName, req->mapName);  // 同尺寸 wire 字段
```

存量代码可逐步对齐，**新加内容必须遵守上述规范**。

## 目录结构

```
RPG/
├── sdk/           # 底层封装（网络、时间、定时器、日志、配置）
│   ├── net/       # NetDefine、MsgId、TcpServer/Client/Connection
│   ├── time/      # 墙钟：TimeUtil、AlarmClock
│   └── timer/     # 单调时钟：TimerMgr（相对间隔调度）
├── common/        # 客户端协议 ClientMsg.h
├── protocal/      # 服务器内部协议 InternalMsg.h
├── GatewayServer/ # 接入 + ClientMsgValidator/Router
├── SceneServer/   # Scene、CopyScene、SceneManager、SceneEntry/Npc/User
├── SessionServer/ # SessionScene、SessionCopyScene、SessionSceneManager
├── config/        # config.xml、server_info.xml
├── DataDoc/       # 策划 Excel 表（源数据）
├── database/      # init.sql + 生成的 *_config.lua 配表
├── basefile/      # 配表加载工具（data_table.lua）
├── script/        # Lua 游戏脚本
├── tools/         # gen_datadoc.py（Excel→Lua）
├── gen_data.sh    # 配表生成入口
├── build          # 编译入口（等价 ./build.sh）
├── build.sh       # 编译脚本
├── autoinit.sh    # 环境初始化
├── RunServer.sh   # 启动所有服务器
├── StopServer.sh  # 停止所有服务器
└── log.sh         # 实时查看日志
```

## 快速上手

### 环境依赖

- g++（C++17）、CMake 3.16+、make、curl
- 构建 MariaDB Connector 时需 **openssl-devel**、**zlib-devel**（仅编译期）

```bash
# CentOS/RHEL
sudo dnf install -y gcc-c++ cmake make curl tar openssl-devel zlib-devel
```

第三方库 **Lua 5.4 / tinyxml2 / MySQL client** 集成在 `3Party/`，无需安装 `libmysqlclient-dev` 等系统包。详见 [3Party/README.md](3Party/README.md)。

### 初始化与编译

```bash
./autoinit.sh          # 下载编译 3Party + cmake configure
./gen_data.sh          # DataDoc Excel → database/*.lua（可选 --init 生成示例表）
./build                # 或 ./build.sh
```

### 策划配表（DataDoc）

| 步骤 | 说明 |
|------|------|
| 编辑 | `DataDoc/*.xlsx` |
| 生成 | `./gen_data.sh` → `database/*_config.lua` |
| 加载 | SceneServer 经 `basefile/data_table.lua` 的 `DataTable.load()` |

详见 [DataDoc/README.md](DataDoc/README.md)。

仅重建第三方库：`./3Party/download_and_build.sh`（加 `--force` 强制重编）

### 数据库

```bash
mysql -u root -p < database/init.sql
# 修改 config/config.xml 中的 Database 密码
```

测试账号：`test001` / `123456`

### 启动

```bash
./RunServer.sh                          # 核心 7 服
ENABLE_GLOBAL=1 ./RunServer.sh          # 含 GlobalServer
ENABLE_ZONE=1 ./RunServer.sh            # 含 ZoneServer
./log.sh                                # 查看日志
./StopServer.sh                         # 停止
```

## 新人阅读顺序

1. `sdk/net/NetDefine.h` + `sdk/net/MsgId.h` — 消息帧与 module/sub
2. `common/ClientMsg.h` — 客户端协议与 `ClientModule`
3. `protocal/InternalMsg.h` — 服间协议与 `Msg_GW_*` 转发结构
4. `GatewayServer/ClientMsgValidator.h` + `ClientMsgRouter.h` — 网关校验与路由
5. `GatewayServer/GatewayServer.h` — 客户端接入与转发
6. `sdk/util/UserBase.h` — 用户模型
7. `SuperServer/SuperServer.h` — 注册与登录调度
8. `SceneServer/SceneServer.h` — 核心游戏逻辑
9. `SessionServer/SessionSceneManager.h` — 全区场景/副本调度

## 场景系统

SceneServer 负责**运行**场景实例，SessionServer 负责**登记与调度**全区所有 SceneServer 上的普通地图与副本。

```
server_info.xml
      ↓
SceneServer (SceneManager)
      ├─ Scene（普通地图）
      └─ CopyScene（副本，CopySceneFactory 按类型创建子类）
            ↓ 启动成功
      AOIServer（AOI_SCENE_REGISTER）
      SessionServer（SES_SCENE_REGISTER_REQ）
            ↓
SessionSceneManager
      ├─ SessionScene（普通地图注册表）
      └─ SessionCopyScene（副本实例注册表 + 复用 + 负载均衡）
```

### 核心类

| 进程 | 类 | 职责 |
|------|-----|------|
| SceneServer | `Scene` | 场景基类：名字/等级/坐标、加载地图资源、玩家列表、状态机 |
| SceneServer | `CopyScene` | 副本基类，继承 `Scene` |
| SceneServer | `TeamCopyScene` / `SoloCopyScene` / `GuildCopyScene` | 按 `CopyType` 分的副本子类 |
| SceneServer | `CopySceneFactory` | 工厂创建副本（新增类型：加子类 + 工厂分支） |
| SceneServer | `SceneManager` | 管理本进程所有普通场景与副本 |
| SessionServer | `SessionScene` | 单个普通地图在 Session 上的注册记录 |
| SessionServer | `SessionCopyScene` | 单个副本实例的注册记录（含复用判断） |
| SessionServer | `SessionSceneManager` | 全区场景/副本管理 + SceneServer 负载均衡 |

`SceneUser` 与 `SceneNpc` 同级继承 `SceneEntry`，运行在具体 `Scene` 实例所在的地图内。

### 普通地图创建流程

1. **SceneServer 启动**：读取 `config/server_info.xml`，`SceneManager.createNormalScenesFromConfig()` 为每个 `<Map>` 创建 `Scene`，调用 `loadResources()` 加载地图资源（`.map` 路径等）。
2. **场景启动成功**：`onSceneStarted()` 依次通知：
   - **AOIServer**：`AOI_SCENE_REGISTER`（登记场景实例，供视野管理）
   - **SessionServer**：`SES_SCENE_REGISTER_REQ`（全区注册，Session 回 `SES_SCENE_REGISTER_RSP`）
3. 普通场景实例 ID：`(sceneServerId << 32) | mapId`（见 `makeNormalSceneInstanceId()`）。

### 副本创建流程

1. **SceneServer 发起**：`requestCreateCopy(copyType, mapId, ownerId, ...)` → `SES_COPY_CREATE_REQ` → SessionServer。
2. **SessionServer 处理**：
   - 按 **copyType + mapId + ownerId** 查找可复用的 `SessionCopyScene`（未满员且 RUNNING）→ 有则直接 `SES_COPY_CREATE_RSP`（`reused=1`）。
   - 无匹配副本 → **负载均衡**选取 SceneServer → 在 Session 创建 `SessionCopyScene` 记录 → 向目标进程发 `SES_COPY_CREATE_CMD`。
3. **目标 SceneServer**：收到 `SES_COPY_CREATE_CMD` 后 `CopySceneFactory.create()` 创建副本 → 启动并注册 AOI + Session（同普通地图）。
4. **请求方**收到 `SES_COPY_CREATE_RSP`，内含 `targetSceneServerId`、`copyInstanceId` 等路由信息。

```cpp
// 示例：请求创建组队副本
SceneServer::Instance()->requestCreateCopy(
    CopyType::TEAM, 3001, teamId, "哥布林洞穴", "map/3001.map", 5);
```

### 相关协议（InternalMsg.h）

| 协议号 | 方向 | 说明 |
|--------|------|------|
| `SES_SCENE_REGISTER_REQ/RSP` | Scene → Session | 场景注册 |
| `SES_SCENE_UNREGISTER` | Scene → Session | 场景注销 |
| `SES_COPY_CREATE_REQ` | Scene → Session | 请求创建/分配副本 |
| `SES_COPY_CREATE_RSP` | Session → Scene | 副本分配结果（含复用标志） |
| `SES_COPY_CREATE_CMD` | Session → Scene | 指示目标 SceneServer 创建副本 |
| `AOI_SCENE_REGISTER/UNREGISTER` | Scene → AOI | AOI 侧场景实例登记 |

### 地图 ID 约定（server_info.xml）

| 范围 | 类型 |
|------|------|
| 1000–1999 | 主城 / 新手村（普通场景） |
| 2000–2999 | 野外地图 |
| 3000–3999 | 副本模板 mapId |
| 4000–4999 | PvP 战场 |

## 登录流程

```
Client ──[module=0x00 Login]──► Gateway（本地校验）
         ──► Record（验证）──► Super ──► Record（加载）──► Scene ──► AOI
Client ◄── Gateway ◄── Super ◄── Scene（S2C_LOGIN_RSP + S2C_ENTER_GAME）

登录后玩法包 ──► Gateway（Validator）──► Scene 或 Session（Router）
```

## 时间库（sdk/time + sdk/timer）

项目把时间能力拆成两层，对应两种时钟语义：

| 目录 | 头文件 | 时钟类型 | 典型用途 |
|------|--------|----------|----------|
| `sdk/time/` | `TimeUtil.h` | `system_clock`（墙钟，Unix 毫秒） | 日志时间戳、字符串解析、日历分量、活动截止 |
| `sdk/time/` | `AlarmClock.h` | 墙钟 + TimerMgr 调度 | 指定时刻 / 每日 / 每周闹钟 |
| `sdk/timer/` | `TimerMgr.h` | `steady_clock`（单调，不受调时影响） | 心跳、Save 间隔、N 毫秒后回调 |

**为何分两个目录？** `time` 处理「几点几分、哪一天」这类日历语义；`timer` 处理「再过 3 秒、每 60 秒」这类相对间隔。两者可以放在同一父目录下，但不宜混成一个类——墙钟会被 NTP/手动调时影响，单调时钟不会。

**可以合并目录吗？** 可以。例如全部放进 `sdk/time/`（`TimeUtil.h`、`AlarmClock.h`、`TimerMgr.h`），只是组织方式变化，API 不变。当前保持 `time/` 与 `timer/` 分开，是为了一眼区分「绝对时刻」与「相对间隔」。

### 常用示例

```cpp
#include "sdk/time/TimeUtil.h"
#include "sdk/time/AlarmClock.h"
#include "sdk/timer/TimerMgr.h"

// 墙钟：当前时间与格式化（Logger 已使用 TimeUtil::Format）
int64_t now = TimeUtil::UnixMs();
std::string s = TimeUtil::Format(now, "%Y-%m-%d %H:%M:%S.%ms");

int64_t ts = 0;
TimeUtil::Parse("2025-05-24 12:00:00", ts);
int days = TimeUtil::DaysBetween(ts, now);

// 相对间隔：3 秒后、每 60 秒
TimerMgr::Instance().Register(3000, 0, []{ /* once */ });
TimerMgr::Instance().Register(60000, 60000, []{ /* repeat */ });

// 绝对闹钟：每天 08:00
AlarmClock::Instance().SetDaily(8, 0, 0, []{ /* daily reset */ });

// 主循环
while (true) {
    server.Poll();
    TimerMgr::Instance().Update();
    AlarmClock::Instance().Update();
}
```

## 日志文件

各服务器通过 `Logger::SetPath("logs/aoi.log")` 双文件落盘：

| 文件 | 用途 |
|------|------|
| `logs/aoi.log` | 实时日志，`./log.sh` 跟踪此文件 |
| `logs/aoi.log.20260524-12` | 按小时归档（跨小时自动切换） |

路径在 `config/config.xml` 的 `<LogPaths>` 中配置。跨小时时实时文件截断重写，仅保留当前小时；历史内容保存在带时间后缀的归档文件中。

## License

Internal project — see repository for details.
