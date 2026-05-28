---
name: 创建MySQL库表
overview: 在服务器上创建 MySQL 数据库与账号 `rpg_table/rpg_table`，并使用 `tables/init.sql` 初始化所有表结构；最后做连通性与表数量验证。
todos: []
isProject: false
---

# 服务器数据库创建计划

## 目标

- 创建数据库账号：`rpg_table` / `rpg_table`
- 创建数据库：`rpg_game`
- 读取并执行 [`/home/hcg/RPG/tables/init.sql`](/home/hcg/RPG/tables/init.sql) 创建表结构

## 执行步骤

1. 使用管理员账号登录 MySQL（通常是 `root`）。
2. 执行 SQL：
   - `CREATE DATABASE IF NOT EXISTS rpg_game DEFAULT CHARACTER SET utf8mb4;`
   - `CREATE USER IF NOT EXISTS 'rpg_table'@'%' IDENTIFIED BY 'rpg_table';`
   - `GRANT ALL PRIVILEGES ON rpg_game.* TO 'rpg_table'@'%';`
   - `FLUSH PRIVILEGES;`
3. 使用应用账号导入表结构：
   - `mysql -u rpg_table -prpg_table rpg_game < /home/hcg/RPG/tables/init.sql`
4. 验证：
   - 用 `rpg_table` 账号登录后执行 `SHOW TABLES;`
   - 抽查关键表（如 `CharBase`、`Relation`、`Friend`、`Mail`、`MapInfo`）是否存在。

## 验证标准

- `rpg_table` 账号可连接数据库 `rpg_game`
- `SHOW TABLES;` 返回 `init.sql` 定义的全部表
- 无权限错误（如 `Access denied`）和无 SQL 语法错误

## 风险与处理

- 若 MySQL 不允许 `CREATE USER`：改用已有管理员手工创建用户后再授权。
- 若远程连接受限：将 `'%'` 改为具体主机（如 `'127.0.0.1'` 或业务网段）。
- 若 `init.sql` 中已含 `CREATE DATABASE/USE`，重复执行也不会破坏结果（幂等）。