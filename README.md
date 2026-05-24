# RPG Server

Linux 平台下的分布式 MMORPG 游戏服务器，采用 **C++17 + Lua 5.4**，共 **9 个独立进程** 组成微服务集群。

## 架构概览

```
Client → GatewayServer → SuperServer → RecordServer → SceneServer → AOIServer
                              ↓              ↓
                        SessionServer    MySQL
```

| 服务器 | 端口 | 职责 |
|--------|------|------|
| SuperServer | 9000 | 注册中心、登录调度 |
| SessionServer | 9001 | 社会关系、离线数据 |
| RecordServer | 9002 | MySQL 持久化 |
| AOIServer | 9003 | 9 宫格视野管理 |
| SceneServer | 9004 | 在线逻辑、Lua 脚本 |
| GatewayServer | 9005 / 19005 | 客户端接入（外网/内网） |
| LoggerServer | 9006 | 集中日志 |
| GlobalServer | 9007 | 全区数据（可选） |
| ZoneServer | 9008 | 跨区转发（可选） |

完整架构说明见 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)。

## 开发约定

| 规范 | 说明 |
|------|------|
| **注释** | 新增代码、文件、配置必须附带必要注释 → [comments-required.mdc](.cursor/rules/comments-required.mdc) |
| **类名** | 大驼峰 PascalCase，如 `UserManager` |
| **变量 / 函数** | 小驼峰 camelCase，如 `userCount`、`getUserName()` |
| **宏 / 常量** | 全大写 + 下划线，如 `MAX_NUM` |

命名细则 → [naming-conventions.mdc](.cursor/rules/naming-conventions.mdc)

存量代码可逐步对齐，**新加内容必须遵守上述规范**。

## 目录结构

```
RPG/
├── sdk/           # 底层封装（网络、时间、定时器、日志、配置）
│   ├── time/      # 墙钟：TimeUtil、AlarmClock
│   └── timer/     # 单调时钟：TimerMgr（相对间隔调度）
├── common/        # 客户端协议 ClientMsg.h
├── protocal/      # 服务器内部协议 InternalMsg.h
├── config/        # config.xml、server_info.xml
├── database/      # init.sql
├── script/        # Lua 游戏脚本
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
./build                # 或 ./build.sh
```

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

1. `sdk/net/NetDefine.h` — 网络基础
2. `sdk/util/UserBase.h` — 用户模型
3. `protocal/InternalMsg.h` — 服务器间协议
4. `common/ClientMsg.h` — 客户端协议
5. `SuperServer/SuperServer.h` — 注册与路由
6. `GatewayServer/GatewayServer.h` — 客户端接入
7. `SceneServer/SceneServer.h` — 核心游戏逻辑

## 登录流程

```
Client → Gateway → Record（验证）→ Super → Record（加载）→ Scene → AOI
                                                              ↓
Client ← Gateway ← Super ← Scene（进入完成）
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
