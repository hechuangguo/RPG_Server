-- ============================================================
--  迁移：Relation 表增加 binary 列（已建库环境执行一次）
--  数据库：rpg_game
--  执行：mysql -h 127.0.0.1 -u rpg_table -prpg_table rpg_game < tables/alter_relation_add_binary.sql
--  说明：若列已存在会报错 Duplicate column，可忽略
-- ============================================================

USE rpg_game;

ALTER TABLE Relation
    ADD COLUMN `binary` MEDIUMBLOB COMMENT '社交扩展数据二进制序列化（申请/缓存等）'
    AFTER team_id;
