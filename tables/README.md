# tables — MySQL 表结构脚本

本目录存放 **所有** 数据库建表与初始化数据脚本，与 [`database/`](../database/)（Lua 策划配表）平级，由 RecordServer 等通过 MySQL 读写。

## 脚本说明

| 文件 | 说明 |
|------|------|
| `database.credentials` | 工作区内连接信息（库名/用户/密码，与 `config.xml` 一致） |
| `create_user_and_db.sql` | 建库 `rpg_game`、创建应用用户 `rpg_table` 并授权（须 root 执行） |
| `setup_database.sh` | 一键：建库建用户 + `init.sql` 建表 + 连接验证 |
| `init.sql` | 建库建表：`CREATE DATABASE`、`USE rpg_game`、全部 `CREATE TABLE` |
| `alter_relation_add_binary.sql` | 迁移：已为旧库 `Relation` 表增加 `` `binary` `` 列（执行一次） |
| `seed_test_data.sql` | 开发/测试用种子数据：test001~test003 初始角色 CharBase 与 Relation |
| `examples_query_characters.sql` | 示例：只查角色（CharBase 多类 SELECT） |
| `examples_batch_update_test_accounts.sql` | 示例：批量改 test001~test003（UPDATE + 恢复 seed 值） |

### 默认账号（开发环境）

| 项 | 值 |
|----|-----|
| 数据库 | `rpg_game` |
| 用户名 | `rpg_table` |
| 密码 | `rpg_table` |

## 执行方式

**推荐：一键初始化**

```bash
chmod +x tables/setup_database.sh
./tables/setup_database.sh
# 或指定 root 密码：MYSQL_ROOT_PASSWORD=你的root密码 ./tables/setup_database.sh
```

**手动分步**

```bash
mysql -u root -p < tables/create_user_and_db.sql
mysql -u root -p < tables/init.sql
```

**第一步（必须）：建库建表**（若未用上面一键脚本）

```bash
mysql -u root -p < tables/init.sql
```

**第二步（可选）：导入开发测试数据**

> 仅在开发/测试环境执行，生产环境请跳过。

```bash
mysql -u root -p < tables/seed_test_data.sql
```

库名须与 [`config/config.xml`](../config/config.xml) 中 `<Database name="..."/>` 一致（默认 `rpg_game`）。  
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
2. 脚本内只写 `CREATE TABLE` / `ALTER` / 该表相关种子数据，**不要**重复 `CREATE DATABASE` / `USE`（由 `init.sql` 统一处理）。
3. 在 `init.sql` 中按**外键/依赖顺序**引用新脚本（合并 `SOURCE` 或复制语句块，保持可重复执行：`IF NOT EXISTS` / `INSERT IGNORE`）。
4. 更新本 README 与 [`../database/README.md`](../database/README.md)（若有文档交叉引用）。

## 当前表一览（init.sql 内）

**玩家与社交**

| 表 | 说明 | C++ 读写 |
|----|------|----------|
| `CharBase` | 账号+角色合并；`` `binary` `` 存 bag/skills/buffs/quests | RecordServer（完整） |
| `Relation` | 好友/黑名单 JSON + 社交扩展 `` `binary` `` | RecordServer + SessionServer |

**仅 DDL（C++ 尚未使用）**

| 表 | 说明 |
|----|------|
| `Friend` | 双向好友/黑名单行（预留） |
| `Mail` | 离线邮件 |
| `MapInfo` | 每用户每地图 JSON 存档 |

**集群与登录**

| 表 | 说明 | 读写进程 |
|----|------|----------|
| `ServerList` | 区内拓扑（Super…Gateway 端口） | SuperServer 启动只读；`init.sql` 种子 |
| `ZoneInfo` | 登录区列表（IP、super_port、enabled） | LoginServer `ZoneInfoStore` |

`Relation.binary`：SessionServer 经 Record 读写，存社交扩展二进制；好友列表仍用 JSON 文本字段。

详见 [DATA.md](../docs/DATA.md)。

**代码与表名约定：** SQL 中的表名须与 `init.sql` 一致（PascalCase：`CharBase`、`Relation`、`Friend`、`Mail`、`MapInfo`），勿使用已废弃的 `t_` 前缀旧名（如 `t_charbase`）。
