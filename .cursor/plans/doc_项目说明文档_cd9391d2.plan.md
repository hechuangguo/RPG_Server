---
name: Doc 项目说明文档
overview: 在仓库根目录 [`Doc/`](Doc/) 下新增一份简洁的项目说明文档，概述 3D MMORPG 整体结构及 Server、Client、Common 的技术栈与职责划分。
todos:
  - id: create-doc-readme
    content: 新建 Doc/README.md，写入项目概述、技术栈、Common 说明与目录结构
    status: completed
isProject: false
---

# Doc 目录项目说明文档

## 背景

- 根目录已有 [`Doc/`](Doc/) 目录，目前为空
- 仓库结构：`Client/`、`Common/`、`Server/` 并列于 [`d:\Study\RPG`](d:\Study\RPG)
- [`Server/README.md`](Server/README.md) 与 [`Server/docs/`](Server/docs/) 侧重**服务端**细节，缺少**全仓库**层面的入门说明

## 新增文件

创建 [`Doc/README.md`](Doc/README.md) 作为项目总览文档（`Doc` 目录下的默认入口）。

## 文档内容结构

按你提供的 4 点组织，并补充简短目录说明，便于新人定位：

```markdown
# RPG 项目说明

## 项目概述
- 3D MMORPG，包含服务器（Server）与客户端（Client）

## 技术栈
### 服务器（Server）
- C++：核心服务进程与网络逻辑
- Lua：场景玩法脚本（如 SceneServer）
- MySQL：数据持久化

### 客户端（Client）
- C++：客户端核心
- Lua：客户端脚本逻辑

## 共用层（Common）
- Server 与 Client 共用内容
- 消息协议、枚举、结构体等（如 ClientMsg.h）

## 目录结构
RPG/
├── Client/    # 客户端工程
├── Common/    # Server / Client 共用
├── Doc/       # 项目说明文档（本目录）
└── Server/    # 服务器工程
```

要点说明（写入文档时展开为完整段落，非 bullet 堆砌）：

| 章节 | 内容 |
|------|------|
| 项目概述 | 3D MMORPG；Server + Client 双端协作 |
| Server | C++ 分布式多进程 + Lua 热更玩法 + MySQL 存档 |
| Client | C++ 引擎/框架 + Lua 业务脚本 |
| Common | 双方需一致的协议与定义；当前已有 [`Common/ClientMsg.h`](Common/ClientMsg.h) |
| 目录结构 | 四目录职责一览；Server 详细文档见 `Server/README.md` |

## 实施步骤

1. 新建 [`Doc/README.md`](Doc/README.md)，写入上述内容（中文，简洁可读）
2. 不修改 Server/Client/Common 代码或现有 Server 文档（本次仅新增根级说明）

## 不在本次范围

- 不修改 [`.cursor/plans/common_共享层复制_f6f0e6c3.plan.md`](.cursor/plans/common_共享层复制_f6f0e6c3.plan.md)
- 不在 Server 内重复维护同一份说明（根 `Doc/` 为全项目入口，Server 文档保持服务端专述）
