# RPG 项目说明

## 项目概述

本项目是一款 **3D MMORPG**，采用 Server（服务器）与 Client（客户端）双端协作架构。服务器负责游戏逻辑、数据持久化与多人在线同步；客户端负责 3D 场景渲染、玩家交互与网络通信。

## 技术栈

### 服务器（Server）

- **C++**：分布式多进程核心服务，涵盖网关、场景、会话、存档等模块，以及 TCP 网络与消息分发。
- **Lua**：场景玩法脚本（如 SceneServer 内的技能、NPC、任务等逻辑），支持热更新。
- **MySQL**：角色、账号、社会关系等游戏数据的持久化存储。

### 客户端（Client）

- **C++**：客户端核心框架，包括渲染、输入、网络与资源管理。
- **Lua**：客户端业务脚本，用于 UI 流程、玩法表现等可热更逻辑。

## 共用层（Common）

`Common` 目录存放 **Server 与 Client 共用** 的内容，确保双端对协议与数据定义保持一致。主要包括：

- 客户端 ↔ 服务器消息协议号与枚举
- 消息结构体（wire 格式）
- 双方共用的常量与类型定义

当前已有 [`Common/ClientMsg.h`](../Common/ClientMsg.h)，定义 `ClientModule`、`ClientMsgID` 及各类 `Msg_C2S_*` / `Msg_S2C_*` 结构体。

## 目录结构

```
RPG/
├── Client/    # 客户端工程
├── Common/    # Server / Client 共用（协议、枚举、结构体等）
├── Doc/       # 项目说明文档（本目录）
└── Server/    # 服务器工程
```

服务器详细架构、构建与运维说明见 [`Server/README.md`](../Server/README.md) 与 [`Server/docs/`](../Server/docs/)。
