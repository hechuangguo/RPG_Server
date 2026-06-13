# sdk — 公共底层库

各 `*Server` 进程共享的网络、定时器、日志、配置与集群 Bootstrap 代码。

**完整说明见 [docs/SDK.md](../docs/SDK.md)**。以下为模块索引。

## 目录

| 子目录 | 职责 | 主要文件 |
|--------|------|----------|
| `net/` | epoll ET TCP、6 字节消息帧 | `NetDefine.h`、`TcpServer.h`、`TcpClient.h`、`MsgId.h` |
| `timer/` | 相对间隔调度（单调时钟） | `TimerMgr.h` |
| `time/` | 墙钟、日历、绝对闹钟 | `TimeUtil.h`、`AlarmClock.h` |
| `log/` | 本地/远程日志 | `Logger.h`、`LogFileWriter.h`、`RemoteLogClient.h` |
| `http/` | HTTP/1.1 解析（GlobalServer） | `HttpParser.h`、`HttpCodec.h` |
| `math/` | 向量与随机数 | `Vec.h`、`Random.h` |
| `util/` | 配置、分发、Bootstrap、外联转发 | `MsgDispatcher.h`、`ConfigLoader.h`、`ServerBootstrap.h`、`ExternalServerHub.h` |

## 构建说明

大部分为头文件；以下 `.cpp` 编入各服二进制：

- `util/`：`ServerList`、`LoginServerList`、`ExternalServerHub`、`GameZoneExternSender`、`GameZoneMsgDispatch`、`ExternalServerConnector`
- `log/`：`RemoteLogClient`、`UserLog`
- `math/`：`Vec`、`Random`
- `http/`：`HttpParser`

## 典型主循环

```cpp
while (true) {
    server.Poll();
    TimerMgr::Instance().Update();
}
```

详见 [docs/SDK.md](../docs/SDK.md) 与各头文件 `@file` 注释。
