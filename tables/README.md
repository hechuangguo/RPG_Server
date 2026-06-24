# tables — MySQL 表结构脚本

本目录存放 **所有** 数据库建表与初始化数据脚本，与 [`database/`](../database/)（Lua 策划配表）平级。

## 三库分工

| 库 | 表 | 连接进程 | 配置 |
|----|-----|----------|------|
| `rpg_login` | GameUser, ZoneInfo, LoginSession | LoginServer | `LoginServer/extern_login.xml` |
| `rpg_game` | CharBase, Relation, Friend, Mail, MapInfo, ServerList | SuperServer, RecordServer, SessionServer | `config/config.xml` |
| `rpg_global` | AllLittleThing | GlobalServer | `GlobalServer/extern_global.xml` |

## 脚本说明

| 文件 | 说明 |
|------|------|
| `database.credentials` | 工作区内连接信息（默认区内库 rpg_game；含三库名注释） |
| `create_user_and_db.sql` | 建三库、创建应用用户 `rpg_table` 并授权（须 root 执行） |
| `setup_database.sh` | 一键：建三库与用户 + `init.sql` 建表 + 三库验证 |
| `init.sql` | 三库建表：rpg_login（3 表）+ rpg_game（6 表）+ rpg_global（1 表） |
| `migrate_login_db.sql` | 存量迁移：GameUser/ZoneInfo 从 rpg_game → rpg_login |
| `migrate_login_session.sql` | 存量迁移：补齐 rpg_login.LoginSession（网关鉴权票据表） |
| `alter_login_flow.sql` | 存量迁移：LoginSession + CharBase.accid/gamezone 列 |
| `alter_character_model.sql` | 存量迁移：CharBase.model_id 列（创角/进游戏角色模型） |
| `alter_relation_add_binary.sql` | 迁移：已为旧库 `Relation` 表增加 `` `binary` `` 列（执行一次） |
| `seed_test_data.sql` | 开发/测试用种子数据：test001~test003（rpg_game） |
| `examples_query_characters.sql` | 示例：只查角色（CharBase 多类 SELECT） |
| `examples_batch_update_test_accounts.sql` | 示例：批量改 test001~test003（UPDATE + 恢复 seed 值） |

### 默认账号（开发环境）

| 项 | 值 |
|----|-----|
| 区内库 | `rpg_game`（`config/config.xml`） |
| 登录库 | `rpg_login`（`extern_login.xml`） |
| 全区库 | `rpg_global`（`extern_global.xml`） |
| 用户名 | `rpg_table` |
| 密码 | `rpg_table` |

## 执行方式

**推荐：一键初始化**

```bash
chmod +x tables/setup_database.sh
./tables/setup_database.sh
# 或指定 root 密码：MYSQL_ROOT_PASSWORD=你的root密码 ./tables/setup_database.sh
```

**存量升级**（旧环境 rpg_game 含 GameUser/ZoneInfo）：

```bash
mysql -u root -p < tables/migrate_login_db.sql
```

**仅补 LoginSession 表**（登录报「会话写入失败」时）：

```bash
mysql -h HOST -u rpg_table -prpg_table rpg_login < tables/migrate_login_session.sql
```

**手动分步**

```bash
mysql -u root -p < tables/create_user_and_db.sql
mysql -u root -p < tables/init.sql
```

**第二步（可选）：导入开发测试数据**

> 仅在开发/测试环境执行，生产环境请跳过。

```bash
mysql -u root -p < tables/seed_test_data.sql
```

区内库名须与 [`config/config.xml`](../config/config.xml) 中 `<Database name="..."/>` 一致（默认 `rpg_game`）。  
`seed_test_data.sql` 可重复执行（`INSERT IGNORE` + 条件 `UPDATE`）。

**已建库、需补 `Relation.binary` 列：**

```bash
mysql -h 127.0.0.1 -u rpg_table -prpg_table rpg_game < tables/alter_relation_add_binary.sql
```

**手动查改示例（需先导入 seed）：**

```bash
mysql -h 127.0.0.1 -u rpg_table -prpg_table rpg_game < tables/examples_query_characters.sql
mysql -h 127.0.0.1 -u rpg_table -prpg_table rpg_game < tables/examples_batch_update_test_accounts.sql
```

## 注释约定

- 每个 `.sql` 文件须有**文件头** `--` 说明（用途、库名、执行顺序、是否可重复执行）。
- 每张表前用 `--` 段描述设计意图与读写进程；字段优先 `COMMENT '...'`。
- 详见 [`docs/COMMENTS.md`](../docs/COMMENTS.md) 与 `.cursor/rules/comments-required.mdc`。

## 新增表约定

1. 在 `tables/` 下新增独立 `.sql`（建议命名 `t_<表名>.sql`）。
2. 确定目标库（rpg_login / rpg_game / rpg_global），在 `init.sql` 对应 `USE` 段按依赖顺序添加。
3. 保持可重复执行：`IF NOT EXISTS` / `INSERT IGNORE`。
4. 更新本 README 与 [`docs/DATA.md`](../docs/DATA.md)。

## 当前表一览（init.sql）

**rpg_login（LoginServer）**

| 表 | 说明 |
|----|------|
| `GameUser` | 账号/密码哈希/区号/绑定角色（password_hash = bcrypt(hex(SHA-256))） |
| `ZoneInfo` | 区服入口参考/种子 |
| `LoginSession` | 登录票据 loginToken（短期一次性；Login 写、Gateway 鉴权消费） |

**rpg_game（区内）**

| 表 | 说明 | C++ 读写 |
|----|------|----------|
| `CharBase` | 角色基础；含 `model_id`；`` `binary` `` 存 bag/skills/buffs/quests | RecordServer |
| `Relation` | 好友/黑名单 JSON + 社交扩展 `` `binary` `` | RecordServer + Session（经协议） |
| `Friend` | 双向好友/黑名单行（预留） | — |
| `Mail` | 离线邮件 | — |
| `MapInfo` | 每用户每地图 JSON 存档 | — |
| `ServerList` | 区内拓扑 | SuperServer 启动只读 |

SessionServer 已直连 rpg_game（本区排行榜等后期玩法）；Relation 仍经 Record 协议读写。

**rpg_global（GlobalServer）**

| 表 | 说明 |
|----|------|
| `AllLittleThing` | 全区杂项 KV（thing_key + thing_value） |

详见 [DATA.md](../docs/DATA.md)。

**代码与表名约定：** SQL 中的表名须与 `init.sql` 一致（PascalCase），勿使用已废弃的 `t_` 前缀旧名。
