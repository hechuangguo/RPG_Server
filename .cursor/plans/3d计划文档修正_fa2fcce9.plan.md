---
name: 3D计划文档修正
overview: 修正 [`3d迁移完整方案_1930feb4.plan.md`](.cursor/plans/3d迁移完整方案_1930feb4.plan.md) 中与已完成的 Protobuf 目录重组矛盾的 `Common/generated/` 描述，并删除 §8 重复的 `maps/README.md` 条目。
todos:
  - id: fix-proto-paths
    content: 更新 3d迁移计划 §2/§3.4/§9 中 Common/generated 与 gen_proto 路径为 Protobuf/ + scripts/gen_proto.sh
    status: completed
  - id: fix-dup-bullet
    content: 删除 §8 L555 重复的 maps/README.md 条目
    status: completed
  - id: verify-grep
    content: grep plan 文件确认无 Common/generated、无重复 maps/README.md
    status: completed
isProject: false
---

# 3D 迁移计划文档修正

## 验证结论

**Bug 1 — 已确认存在**

[`.cursor/plans/3d迁移完整方案_1930feb4.plan.md`](.cursor/plans/3d迁移完整方案_1930feb4.plan.md) 仍多处描述旧布局，与已落地的 [`.cursor/plans/protobuf_目录重组_d8237a02.plan.md`](.cursor/plans/protobuf_目录重组_d8237a02.plan.md) / 当前代码不一致：

| 行号 | 过时内容 |
|------|----------|
| 66–67 | `Common/generated/cpp/`、`Common/generated/csharp/` |
| 287–291 | 目录树含 `generated/`、`tools/gen_proto.sh` |
| 88 | `Common/tools/gen_proto.sh` |
| 571 | 新增脚本写 `Common/tools/gen_proto.sh` |

当前真源（已实现）：

- **Common/**：仅 `*.proto` + `ClientTypes.h` / `NetDefine.h` / `MsgId.h`
- **Protobuf/**：Server C++ 生成物（[`scripts/gen_proto.sh`](scripts/gen_proto.sh)）
- **Client**：自行从 Common `.proto` 生成 C#（Common 内无 `generated/`）

**Bug 2 — 已确认存在**

§8 交付物清单 L553 与 L555 完全相同：

```markdown
- [`maps/README.md`](maps/README.md) — runtime 目录说明
- [`maps/README.md`](maps/README.md) — runtime 目录说明
```

---

## 修复范围（单文件）

仅编辑 [`.cursor/plans/3d迁移完整方案_1930feb4.plan.md`](.cursor/plans/3d迁移完整方案_1930feb4.plan.md)（用户要求不改其他 plan 文件）。

### 1. §2 协议决策表（约 L59–67）

| 原行 | 改为 |
|------|------|
| 真源位置「与 `*Msg.h` 同路径」 | 「RPG_Common submodule，仅 `.proto` + 路由 `.h`」 |
| 文件规则「对照 `XxxCommon.h` + `XxxMsg.h`」 | 「`XxxCommon.proto` + `XxxMsg.proto` 成对」 |
| C++ `Common/generated/cpp/` | `libprotobuf` + 主仓 **`Protobuf/`**（CMake 链接） |
| C# `Common/generated/csharp/` | Client 工程内自行 `protoc`（Common 不含生成物） |

### 2. §2 workflow 与注释（约 L54、L73、L88）

- L54：去掉「与 `*Msg.h` 同目录」
- L73：「与 `*Msg.h` 同级」→「与 [`docs/COMMENTS.md`](docs/COMMENTS.md) §Common 协议同级」
- L88：`Common/tools/gen_proto.sh` → **`./scripts/gen_proto.sh`**（输出 `Protobuf/`）

### 3. §3.4 目录树（约 L287–291）

替换为与 [`docs/3D_DESIGN.md`](docs/3D_DESIGN.md) §4 一致的两段结构：

```
Common/          # submodule：仅 *.proto + ClientTypes.h / NetDefine.h / MsgId.h
Protobuf/        # 主仓：*.pb.h / *.pb.cc（scripts/gen_proto.sh）
scripts/gen_proto.sh
```

删除 `generated/`、`tools/gen_proto.sh` 节点。

### 4. §8 交付物清单（L551–556）

- **删除** L555 重复项
- 可选补一条：`Protobuf/README.md` — 生成物说明（与重组计划对齐）

### 5. §9 构建脚本（约 L571、L580）

- L571：`Common/tools/gen_proto.sh` → **`scripts/gen_proto.sh`**
- L580：「protoc 生成 C++/C#」→「Server：`scripts/gen_proto.sh` → `Protobuf/`；Client：自行 protoc」

---

## 验证

编辑后在该 plan 文件内 grep，应无：

```
Common/generated
Common/tools/gen_proto
generated/csharp
```

且 §8 中 `maps/README.md` 仅出现一次。

**不跑编译**（纯文档/plan 修正）；与已实现的 [`docs/3D_DESIGN.md`](docs/3D_DESIGN.md) 交叉核对即可。
