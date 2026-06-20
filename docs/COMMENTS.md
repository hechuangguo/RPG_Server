# 注释规范（头文件 / XML / SQL / 源码）

本文档与 [`.cursor/rules/comments-required.mdc`](../.cursor/rules/comments-required.mdc) 一致，供人类开发者与 AI 助手共同遵循。**所有新增与修改**默认按此执行。

## 适用范围

| 类型 | 路径示例 | 注释形式 |
|------|----------|----------|
| C++ 头文件 | `**/*.h` | Doxygen：`@file`、`@brief`、`@param`、`@return`、`/**< */` |
| **Common Protobuf** | `Common/*.proto` | 文件头块注释 + `//` 行注释；见 §Common Protobuf |
| C++ 实现 | `**/*.cpp` | 文件头 + 非显然逻辑 |
| 运行时 XML | `config/config.xml`、`config/server_info.xml` | `<!-- -->` |
| SQL | `tables/*.sql` | `--` 块 + 列 `COMMENT` |
| Lua / Shell / CMake | 各目录 | 文件头 + 关键步骤 |

## C++ 头文件要求

1. **文件头**（每个 `.h` 必须有）  
   说明模块职责、依赖服务、是否涉及存档/单线程约束。

2. **类型**  
   - `class` / `struct` / `enum class`：`@brief`  
   - 枚举值：`/**< 说明 */`

3. **对外 API**  
   - 每个 public 方法：`@brief`  
   - 参数/返回值非显然时：`@param`、`@return`

4. **成员**  
   - `dirty`、`snapshot`、容量常量、映射表等：`/**< */`

5. **排版**（见下节「头文件排版」）

参考：`SessionServer.h`、`protocal/InternalMsg.h`、`SceneServer/BagManager.h`。

## 头文件排版

范本：[`SceneServer/SceneUserManager.h`](../SceneServer/SceneUserManager.h)

| 项 | 约定 |
|----|------|
| 方法单元 | `/** @brief ... */`（若有）+ 方法声明（含 header 内联函数体） |
| 方法之间 | **每个方法单元结束后空一行**，再写下个方法的注释/声明 |
| 注释与声明 | 注释块与方法声明之间**不**额外空行 |
| 访问段 | `public` / `protected` / `private` 切换前保留空行 |
| 适用范围 | **仅函数/方法**；**不含** struct 字段、枚举值、typedef/using |

**正确示例：**

```cpp
    /** @brief 按 userId 查找在线用户 */
    std::shared_ptr<SceneUser> findUser(UserID userId) const;

    /** @brief 注册在线用户 */
    bool addUser(UserID userId, std::shared_ptr<SceneUser> user);
```

**错误示例**（方法贴在一起、缺少空行）：

```cpp
    bool contains(UserID userId) const;
    /** @brief 按 userId 查找 RecordUser */
    std::shared_ptr<RecordUser> findUser(UserID userId) const;
```

协议 struct 的字段成员、枚举值列表保持原样；仅 class/struct **内的函数/方法** 适用本规则。

## Common 协议（RPG_Common）

路径：`Common/` 子模块（`ClientTypes.h`、`*.proto`、`generated/`、`NetDefine.h`）。

### Protobuf 注释（必遵）

与 Doxygen 习惯对齐；CI [`scripts/check_common_proto.sh`](../scripts/check_common_proto.sh) 对下列项做冒烟校验，**缺失则 `Build.sh` 阻断**。

| 类型 | 要求 |
|------|------|
| **文件头** | 每个 `.proto` 顶部**块注释**（`//` 多行）：`@file` 文件名、`@brief` 域与 `ClientModule`、`import` 说明 |
| **`XxxCommon.proto` enum** | 每个枚举值一行注释：**方向**（C→S/S→C）、**sub 十六进制**、**处理进程**（Login/Gateway/Scene/Session）、简述 |
| **业务 enum** | 如 `MoveType`、`GatewayValidateCode`：每个值 `//` 说明含义与关联配置 |
| **`message`** | 每个 C2S/S2C message **前**块注释：**方向**、**module/sub**、**触发时机**；`repeated`/变长须写布局（如 `完整列表 = N × Entry`） |
| **字段** | 每个 field **行尾或上一行** `//`：单位、合法范围、0/空/default 含义、是否必填 |
| **`reserved`** | 删除 field number 时用 `reserved N;` 并注释原字段名与废弃版本/原因 |
| **`import`** | 非显然依赖在文件头或 import 行注释用途（如 `// Vec3`） |
| **占位域** | 未实现的 sub 在 `XxxMsg.proto` 顶部用 `// RESERVED:` 块登记计划 message 名与处理方，不建空 message |

**注释形式：** 优先 `//`；块说明可用 `/* ... */`。**勿**在块注释内写未转义的 `*/` 子串（与 C++ 相同）。

**与 Doxygen 对齐：** 文件头推荐使用 `// @file`、`// @brief`；`protoc --csharp_out` 可将部分注释带入 C# XML 文档（视 `--csharp_opt` 而定）。

范本：`Common/LoginMsg.proto`、`Common/LoginCommon.proto`。人类可读工作流见 [`COMMON.md`](COMMON.md)。

### 范本：`MapDataCommon.proto`（enum）

```protobuf
// @file MapDataCommon.proto
// @brief 地图域公共 enum（ClientModule::SCENE = 0x01）
// 对照 MapDataCommon.h；wire message 见 MapDataMsg.proto

syntax = "proto3";
package rpg.mapdata;

enum MapDataMsgSub {
  MAP_DATA_MSG_SUB_UNSPECIFIED = 0;
  C2S_MOVE_REQ = 1;       // 0x01 C→S 移动请求；Gateway→SceneServer
  S2C_MOVE_NOTIFY = 2;    // 0x02 S→C 移动广播；Scene→Gateway→客户端
  S2C_SPAWN_ENTITY = 5;   // 0x05 S→C 实体进视野；AOI/Scene 下行
}

enum MoveType {
  MOVE_TYPE_UNSPECIFIED = 0;
  MOVE_TYPE_WALK = 1;     // 走；步长上限见 map.meta.json maxStepWalk
  MOVE_TYPE_RUN = 2;      // 跑；步长上限见 maxStepRun
}
```

### 范本：`MapDataMsg.proto`（message）

```protobuf
// @file MapDataMsg.proto
// @brief 地图域 wire message（module=SCENE/0x01）
// sub 枚举见 MapDataCommon.proto

syntax = "proto3";
package rpg.mapdata;

import "ClientCommon.proto";
import "MapDataCommon.proto";

// C→S module=0x01 sub=0x01（MapDataMsgSub::C2S_MOVE_REQ）
// 触发：客户端摇杆/点击地面；Gateway Validator 后转 SceneServer
message C2SMoveReq {
  uint32 entity_id = 1;       // 移动实体 ID，通常为本角色
  rpg.client.Vec3 pos = 2;      // 目标世界坐标，Y-up
  MoveType move_type = 3;       // WALK/RUN；服务端 MoveValidator 校验步长
}

// S→C module=0x01 sub=0x05（MapDataMsgSub::S2C_SPAWN_ENTITY）
// 触发：AOI 进视野或首次进图；Gateway 下行
message S2CSpawnEntity {
  uint32 entity_id = 1;
  uint32 template_id = 2;       // 策划模板 ID（配表）
  rpg.client.Vec3 pos = 3;
  float rotation_y = 4;         // 绕 Y 轴朝向；弧度或度由双端常量约定
  uint32 model_id = 5;          // Unity Addressable/Prefab key，0=默认外观
  uint32 anim_state = 6;        // 客户端动画状态机枚举
}
```

### 新增 / 修改消息 workflow

与 [`Common/README.md`](../Common/README.md) workflow 一致：

1. **`ClientTypes.h`** — 新域则补 `ClientModule`
2. **`XxxCommon.proto`** — 增加 `XxxMsgSub` 枚举值（注释含方向、sub、处理方）
3. **`XxxMsg.proto`** — `import` 对应 Common；定义 `C2S*` / `S2C*` message 及字段注释
4. **`scripts/gen_proto.sh`** — 生成 `Protobuf/*.pb.h` / `*.pb.cc`
5. **Server** — Gateway Validator 登记 module/sub；handler 使用生成 C++ 类型
6. **Client** — 自行从 Common `.proto` 生成对应语言代码

改协议时同步更新 [`PROTOCOL.md`](PROTOCOL.md) 消息表索引（若已登记）。

### Protobuf 提交前自检

- [ ] 每个 touched 的 `.proto` 有文件头块注释（`@file` / `@brief` / 对照 `.h`）
- [ ] 每个 `message` 有方向 + module/sub + 触发时机
- [ ] `XxxCommon.proto` 每个 enum 值有方向、sub、处理方
- [ ] 非显然字段均有行注释；删除字段已 `reserved` 并说明原因
- [ ] 已运行 `./scripts/gen_proto.sh`（`Protobuf/` 由 Build 自动生成，默认不入库）
- [ ] `./scripts/check_common_proto.sh` 通过
- [ ] `./Build.sh` 编译通过

参考架构说明：[`3D_DESIGN.md`](3D_DESIGN.md) §4.3、§11。

## XML 配置要求

1. **文件顶部**：多行 `<!-- -->` 说明文件名、职责、读取方、修改后是否重启。  
2. **配置段**：`<Database>`、各 `<XxxServer port>`、`<LogPaths>` 等段前注释用途。  
3. **关键属性**：host/port/name 等与运维、安全相关的属性要有说明。

参考：`config/config.xml`、`config/server_info.xml`。

## SQL 脚本要求

1. **文件头**：库名、执行顺序（如先 `init.sql` 再 `seed_test_data.sql`）、幂等策略。  
2. **每张表前**：设计意图、由哪个进程读写、外键关系。  
3. **字段**：使用 `COMMENT '...'`；复杂业务在表头 `--` 段补充。  
4. **种子脚本**：标明仅 dev/test、测试账号约定。

参考：`tables/init.sql`、`tables/seed_test_data.sql`。

## 与 Cursor Rules 的关系

| 规则 | 内容 |
|------|------|
| `comments-required.mdc` | alwaysApply，工具默认加载 |
| `naming-conventions.mdc` | 命名（与注释互补） |
| `project.mdc` | 架构与目录 |

## 日志文案规范（新增与修改必遵）

1. `LOG_INFO/WARN/ERR/FATAL/DEBUG` 文案统一使用中文，不新增英文整句日志。  
2. 同一术语保持一致，不混用中英文；推荐映射：
   - `SuperServer` → 超级服
   - `LoginServer` → 登录服
   - `GatewayServer` → 网关服
   - `SessionServer` → 会话服
   - `SceneServer` → 场景服
   - `RecordServer` → 存档服
   - `AOIServer` → 视野服
   - `GlobalServer` → 全局服
   - `ZoneServer` → 跨区服
   - `LoggerServer` → 日志服
3. 日志应包含关键上下文（如 `conn`、`userId`、`mapId`、`code`），避免“失败/成功”但无定位信息。  
4. 修改存量文件时，若触及日志，顺带统一该文件内可见的不一致术语。

## 提交前自检

- [ ] 本次修改涉及的 `.h` / **`.proto`** / `.xml` / `.sql` 均已补齐注释  
- [ ] 新 public API、协议 ID、**Protobuf message/field**、配置项、表字段有说明  
- [ ] 注释解释约束与用途，非逐行翻译代码  
- [ ] C++ 块注释内无 `*/` 子串  
- [ ] class/struct 内相邻方法声明之间有空行（范本 `SceneServer/SceneUserManager.h`）  
- [ ] 改 `Common/*.proto` 时已跑 `check_common_proto.sh`（见 §Common Protobuf）

## 示例：SceneServer 管理器

`SceneUser` 仅**持有**各管理器成员；`init/loop/needSave/save/load/add/remove` 由各管理器类自行实现，不在 `SceneUser` 内做类型分发。参见 `SceneServer/ItemManager.h`、`SceneServer/BagManager.h`。
