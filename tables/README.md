# tables — MySQL 表结构脚本

本目录存放 **所有** 数据库建表与初始化数据脚本，与 [`database/`](../database/)（Lua 策划配表）平级，由 RecordServer 等通过 MySQL 读写。

## 脚本说明

| 文件 | 说明 |
|------|------|
| `init.sql` | 建库建表：`CREATE DATABASE`、`USE rpg_game`、全部 `CREATE TABLE` |
| `seed_test_data.sql` | 开发/测试用种子数据：test001~test003 初始角色 CharBase 与 Relation |

## 执行方式

**第一步（必须）：建库建表**

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

角色基础：`CharBase`  
社交：`Relation`、`Friend`  
其它：`Mail`、`MapInfo`
