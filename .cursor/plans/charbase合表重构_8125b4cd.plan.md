---
name: charbase合表重构
overview: 按你的要求重构 SQL：将 `t_account + t_user + t_charbase` 合并为单表 `charbase`，新增 `MEDIUMBLOB binary` 字段；删除 `t_bag/t_skill/t_quest`；并重写 `seed_test_data.sql` 为新表测试数据。
todos:
  - id: rewrite-init-charbase
    content: 重写 init.sql：新增单表 charbase（含 MEDIUMBLOB binary），删除 t_account/t_user/t_charbase/t_bag/t_skill/t_quest
    status: completed
  - id: rewrite-seed
    content: 重写 seed_test_data.sql：删除旧账号绑定数据，改为 charbase 与 relation 测试数据
    status: completed
  - id: consistency-check
    content: 检查两份 SQL 注释与字段 COMMENT 完整，并确认无旧表名残留
    status: completed
isProject: false
---

# init.sql / seed_test_data.sql 优化计划

## 目标变更

基于你已确认的选择：
- `charbase` **不保留** `account/password`（即彻底去掉账号表设计）
- 二进制字段类型使用 `MEDIUMBLOB`

将执行：
1. 在 [`/home/hcg/RPG/tables/init.sql`](/home/hcg/RPG/tables/init.sql) 中合并 `t_account` + `t_user` + `t_charbase` 为单表 `charbase`。
2. 删除 `t_bag`、`t_skill`、`t_quest` 建表段。
3. 在 `charbase` 增加 `binary MEDIUMBLOB`（用于包裹/技能/状态/任务等序列化集合）。
4. 在 [`/home/hcg/RPG/tables/seed_test_data.sql`](/home/hcg/RPG/tables/seed_test_data.sql) 中删除旧数据，改为新 `charbase` 和保留表（如 `t_relation`）的测试数据。

## 具体改动点

### 1) `init.sql`

- 删除以下表定义段：
  - `CREATE TABLE IF NOT EXISTS t_account (...)`
  - `CREATE TABLE IF NOT EXISTS t_user (...)`
  - `CREATE TABLE IF NOT EXISTS t_charbase (...)`
  - `CREATE TABLE IF NOT EXISTS t_bag (...)`
  - `CREATE TABLE IF NOT EXISTS t_skill (...)`
  - `CREATE TABLE IF NOT EXISTS t_quest (...)`
- 新增单表：
  - `CREATE TABLE IF NOT EXISTS charbase (...)`
  - 字段包含当前角色核心属性（`user_id/name/level/vocation/sex/map_id/pos_x/pos_y/pos_z/hp/max_hp/mp/max_mp/gold`）
  - 新增：`binary MEDIUMBLOB COMMENT '包裹/技能/Buff/任务等二进制序列化数据集合'`
  - 继续保留 `update_time`，并补齐表头注释与字段 `COMMENT`
- 其余现存表（`t_relation/t_friend/t_mail/t_map_archive`）保持不动。

### 2) `seed_test_data.sql`

- 删除当前依赖 `t_account` 的测试数据与绑定语句。
- 重写为：
  - 向 `charbase` 插入 1~3 条测试角色（`INSERT IGNORE`）
  - `binary` 字段写入空二进制占位（如 `x''`）或简单样例（可读十六进制）
  - 保留并调整 `t_relation` 的测试占位（与 `charbase.user_id` 一致）
- 保持脚本幂等（`INSERT IGNORE`）。

## 注释与规范

- 按项目规则为 SQL 保留完整文件头、分段说明、表设计意图、字段 `COMMENT`。
- 注释中明确：`binary` 字段承载原 `bag/skill/quest + buff/task` 统一二进制集合。

## 风险提示（会在结果里再次提醒）

- 该变更会使现有依赖 `t_account/t_user/t_charbase` 的登录与读写 SQL **不再兼容**。
- 本次按你要求仅改 SQL 文件，不同步改 C++ 查询逻辑。

## 验证

- 语法层：检查 `init.sql` / `seed_test_data.sql` 无旧表引用残留（`t_account/t_user/t_bag/t_skill/t_quest`）。
- 幂等层：`seed_test_data.sql` 仍可重复执行（`INSERT IGNORE`）。