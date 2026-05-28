# 注释规范（头文件 / XML / SQL / 源码）

本文档与 [`.cursor/rules/comments-required.mdc`](../.cursor/rules/comments-required.mdc) 一致，供人类开发者与 AI 助手共同遵循。**所有新增与修改**默认按此执行。

## 适用范围

| 类型 | 路径示例 | 注释形式 |
|------|----------|----------|
| C++ 头文件 | `**/*.h` | Doxygen：`@file`、`@brief`、`@param`、`@return`、`/**< */` |
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

批量整理存量头文件可运行：[`tools/format_header_methods.py`](../tools/format_header_methods.py)


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

## 提交前自检

- [ ] 本次修改涉及的 `.h` / `.xml` / `.sql` 均已补齐注释  
- [ ] 新 public API、协议 ID、配置项、表字段有说明  
- [ ] 注释解释约束与用途，非逐行翻译代码  
- [ ] C++ 块注释内无 `*/` 子串  
- [ ] class/struct 内相邻方法声明之间有空行（范本 `SceneServer/SceneUserManager.h`）

## 示例：SceneServer 管理器

`SceneUser` 仅**持有**各管理器成员；`init/loop/needSave/save/load/add/remove` 由各管理器类自行实现，不在 `SceneUser` 内做类型分发。参见 `SceneServer/ItemManager.h`、`SceneServer/BagManager.h`。
