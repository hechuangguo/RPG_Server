---
name: Login注册账号
overview: 为 LoginServer 增加独立账号注册体系：新增 GameUser 表、注册校验与写库、bcrypt 密码存储，并将登录校验切换到 GameUser 真源。
todos:
  - id: ddl-gameuser
    content: 在 tables/init.sql 新增 GameUser 表与索引/注释
    status: completed
  - id: protocol-register
    content: 在 Common/ClientMsg.h 定义注册请求/响应协议并同步子模块
    status: completed
  - id: password-util
    content: 新增 PasswordUtil（bcrypt 哈希与校验）
    status: completed
  - id: login-register-service
    content: 实现 LoginRegisterService：参数校验、区校验、重复校验、写 GameUser
    status: completed
  - id: login-route
    content: 在 LoginServer::onClientMessage 挂载注册路由
    status: completed
  - id: login-auth-migrate
    content: 将 LoginAuthService 登录校验切到 GameUser + bcrypt
    status: completed
  - id: docs-tests
    content: 更新协议/数据文档并完成注册登录联调验证
    status: completed
isProject: false
---

# LoginServer 注册账号实现计划

## 目标

围绕 `plan_docs/getzonelist.txt` 第 10–19 条，落地以下能力：
- 客户端注册请求（账号/密码/确认密码/区服）
- LoginServer 注册校验（区状态、重复账号）
- 写入 `GameUser` 并分配 `accid`
- 密码使用 `bcrypt` 哈希存储
- 登录流程从 `GameUser` 校验账号密码，再返回已有 `userId`（若有）

## 数据模型与 SQL

修改 [`tables/init.sql`](tables/init.sql)：
- 新增 `GameUser` 表（`accid` 自增主键、`account` 唯一、`password_hash`、`user_id`、`gamezone`、`create_time`、`update_time`）
- 增加必要索引：`uk_account`、`idx_user_id`、`idx_gamezone`
- 字段注释完整（遵循 SQL 注释规范）

建议结构：
- `accid BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY`
- `account VARCHAR(64) NOT NULL UNIQUE`
- `password_hash VARCHAR(128) NOT NULL`
- `user_id INT UNSIGNED NOT NULL DEFAULT 0`
- `gamezone INT UNSIGNED NOT NULL`
- `create_time DATETIME DEFAULT CURRENT_TIMESTAMP`
- `update_time DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP`

## 协议扩展（客户端）

修改 [`Common/ClientMsg.h`](Common/ClientMsg.h)（需同步 RPG_Common）：
- 新增 `C2S_REGISTER_REQ` / `S2C_REGISTER_RSP`（若当前定义缺失或不满足字段）
- `Msg_C2S_RegisterReq` 包含：`account`、`password`、`confirmPassword`、`zoneId`、`gameType`
- `Msg_S2C_RegisterRsp` 包含：`code`、`msg`、`accid`
- 约定错误码：重复账号、区不可用、参数非法、服务器错误

## LoginServer 业务拆分

### 1) 新增注册服务
新增文件：
- [`LoginServer/LoginRegisterService.h`](LoginServer/LoginRegisterService.h)
- [`LoginServer/LoginRegisterService.cpp`](LoginServer/LoginRegisterService.cpp)

职责：
- 参数校验（长度、密码一致性）
- 区状态校验：调用 `ZoneInfoStore::isZoneEnabled(gameType, zoneId)`
- 账号重复检查：`SELECT accid FROM GameUser WHERE account=?`
- bcrypt 哈希并入库：`INSERT INTO GameUser (...)`
- 返回 `S2C_REGISTER_RSP`

### 2) 路由接入
修改 [`LoginServer/LoginServer.cpp`](LoginServer/LoginServer.cpp)：
- 在 `onClientMessage` 中新增 `sub == registerSub` 路由到 `LoginRegisterService`

### 3) 登录改造为 GameUser 真源
修改 [`LoginServer/LoginAuthService.cpp`](LoginServer/LoginAuthService.cpp)：
- 移除直接以 `CharBase.name` 当账号的逻辑
- 登录流程改为：
  1. `SELECT accid,password_hash,user_id,gamezone FROM GameUser WHERE account=?`
  2. bcrypt 比对密码
  3. 失败返回登录错误；成功返回 `userId`（无角色则 0）
  4. 下发网关时优先按请求区（并校验区状态）

## 密码安全实现

优先使用系统库 `crypt(3)` + bcrypt 前缀（`$2b$`）或引入稳定 bcrypt 实现。

实现点：
- 新增工具：[`sdk/util/PasswordUtil.h`](sdk/util/PasswordUtil.h)、[`sdk/util/PasswordUtil.cpp`](sdk/util/PasswordUtil.cpp)
- 暴露接口：
  - `bool hashPasswordBcrypt(const std::string& plain, std::string& outHash)`
  - `bool verifyPasswordBcrypt(const std::string& plain, const std::string& hash)`
- 不可逆存储，不实现“解密后比对”

## 与角色表关系（user_id 对齐）

- `GameUser.user_id` 与 `CharBase.user_id`、`Relation.user_id` 对齐
- 注册成功时先写 `GameUser`，`user_id=0`
- 后续创建角色成功后再回填 `GameUser.user_id`

（本次若未覆盖创角流程，只保证字段预留与登录返回 0 兼容现有客户端流程）

## 文档同步

更新：
- [`docs/PROTOCOL.md`](docs/PROTOCOL.md)：注册消息与错误码
- [`docs/DATA.md`](docs/DATA.md)：`GameUser` 表职责与字段
- [`docs/EXTERNAL.md`](docs/EXTERNAL.md)：Login 注册/登录链路说明

## 验证计划

1. 建表：执行 `tables/init.sql` 后确认 `GameUser` 存在
2. 注册成功：新账号返回 `accid>0`
3. 重复注册：返回“账号已存在”
4. 区服关闭注册：`enabled=0` 返回区不可用
5. 登录正确密码成功、错误密码失败
6. DB 验证：`password_hash` 非明文，`user_id` 初始 0

## 关键风险

- `Common/ClientMsg.h` 在子模块，需双仓同步（RPG_Server + RPG_Client）
- bcrypt 实现与部署环境库兼容性（需在构建机验证）
- 旧账号（存于 CharBase）迁移策略：本次默认不自动迁移，必要时追加迁移脚本