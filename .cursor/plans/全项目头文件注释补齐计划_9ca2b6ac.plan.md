---
name: 全项目头文件注释补齐计划
overview: 按“全项目所有 .h + 严格粒度”补齐注释：为服务器、sdk、common、protocal 头文件中的方法声明、变量声明、常量、宏、枚举提供统一风格注释，并通过编译与抽样运行验证无回归。
todos:
  - id: scan-all-headers
    content: 扫描并分组全项目 .h，建立注释缺口清单
    status: completed
  - id: comment-protocol-headers
    content: 补齐 common/protocal 头文件的严格注释
    status: completed
  - id: comment-sdk-headers
    content: 补齐 sdk 目录头文件的严格注释
    status: completed
  - id: comment-server-headers
    content: 补齐 9 个服务器目录头文件的严格注释
    status: completed
  - id: lint-build-run-verify-comments
    content: 完成 lint + 全9服编译 + RunServer/StopServer 验证
    status: completed
isProject: false
---

# 全项目头文件注释补齐计划

## 目标

- 覆盖全项目所有 `.h` 文件（含 `sdk/`、`common/`、`protocal/`、各 `*Server/`）。
- 按严格粒度补注释：
  - 方法声明（含 `public/protected/private`）
  - 变量声明（含成员变量、重要局部静态声明）
  - 常量、宏、枚举（含枚举值）
- 注释风格与现有规则一致（Doxygen 风格，强调 why/约束，不复述代码字面）。

## 范围清单（目录级）

- [`/home/hcg/RPG/sdk`](/home/hcg/RPG/sdk)
- [`/home/hcg/RPG/common`](/home/hcg/RPG/common)
- [`/home/hcg/RPG/protocal`](/home/hcg/RPG/protocal)
- [`/home/hcg/RPG/SuperServer`](/home/hcg/RPG/SuperServer)
- [`/home/hcg/RPG/SessionServer`](/home/hcg/RPG/SessionServer)
- [`/home/hcg/RPG/RecordServer`](/home/hcg/RPG/RecordServer)
- [`/home/hcg/RPG/AOIServer`](/home/hcg/RPG/AOIServer)
- [`/home/hcg/RPG/SceneServer`](/home/hcg/RPG/SceneServer)
- [`/home/hcg/RPG/GatewayServer`](/home/hcg/RPG/GatewayServer)
- [`/home/hcg/RPG/LoggerServer`](/home/hcg/RPG/LoggerServer)
- [`/home/hcg/RPG/GlobalServer`](/home/hcg/RPG/GlobalServer)
- [`/home/hcg/RPG/ZoneServer`](/home/hcg/RPG/ZoneServer)

## 执行策略

1. **基线扫描与分组**
   - 先统计所有 `.h` 文件并按模块分组。
   - 标记“注释缺口密集”的头文件优先处理（服务器核心头、协议头、SDK 核心头）。

2. **统一注释模板**
   - 类/结构体：`@brief` + 职责边界。
   - 方法：`@brief` + `@param/@return`（非显然参数必写）。
   - 变量：使用 `/**< ... */` 注释语义与约束。
   - 宏/常量：补用途、单位、边界/默认值。
   - 枚举：枚举类型 `@brief` + 枚举值逐项注释。

3. **分批补齐（按模块推进）**
   - 批次 A：`common/` + `protocal/`（协议优先，影响面最大）
   - 批次 B：`sdk/`（基础库与工具类）
   - 批次 C：9 个服务器目录头文件（业务核心）
   - 每批完成后做一次快速 lint/编译抽查，避免末尾集中爆错。

4. **一致性收尾**
   - 统一术语（如 connID、userID、sceneID、module/sub）。
   - 清理“复述代码式”注释，保留有信息量注释。

5. **验证**
   - 静态检查：读取 lints，修复新增注释引入的格式问题。
   - 编译：`./Build.sh SuperServer SessionServer RecordServer AOIServer GatewayServer LoggerServer SceneServer GlobalServer ZoneServer`
   - 运行抽验：`./RunServer.sh` + `./StopServer.sh`。

## 风险与控制

- **风险：** 文件数量多，单次改动过大影响审阅。
  - **控制：** 按模块分批提交结果，保持“仅注释改动”。
- **风险：** 注释过度导致噪音。
  - **控制：** 优先解释语义/约束/单位，不写无信息量注释。
- **风险：** 协议注释不一致引发误解。
  - **控制：** 先统一 `common/` 与 `protocal/` 术语，再扩散到服务器头。

## 验收标准

- 全项目 `.h` 中：方法声明、变量声明、常量、宏、枚举均有注释覆盖。
- 注释符合现有 Doxygen 风格与项目规则。
- 9 服编译通过，启停链路正常。