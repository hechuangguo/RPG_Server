-- ============================================================
--  示例：批量改测试账号（test001 / test002 / test003）
--  数据库：rpg_game
--  执行：mysql -h 127.0.0.1 -u rpg_table -prpg_table rpg_game < tables/examples_batch_update_test_accounts.sql
--  前提：已导入 seed_test_data.sql；仅开发/测试环境使用
--  用法：取消注释需要执行的一段，其余保持注释，避免互相覆盖
--  备份：mysqldump -h 127.0.0.1 -u rpg_table -prpg_table rpg_game CharBase Relation > backup.sql
-- ============================================================

USE rpg_game;

-- -----------------------------------------------------------
-- 改前快照
-- -----------------------------------------------------------
SELECT user_id, name, level, map_id, hp, max_hp, gold
FROM CharBase
WHERE name IN ('test001', 'test002', 'test003')
ORDER BY user_id;

-- -----------------------------------------------------------
-- 【方案 A】三个测试号统一：20 级、满血蓝、每人 +10000 金币
-- 取消下面整块注释后执行
-- -----------------------------------------------------------
/*
UPDATE CharBase
SET
    level  = 20,
    hp     = max_hp,
    mp     = max_mp,
    gold   = gold + 10000
WHERE name IN ('test001', 'test002', 'test003');
*/

-- -----------------------------------------------------------
-- 【方案 B】恢复为 seed_test_data.sql 初始值（推荐联调前重置）
-- 默认启用；若要用方案 A/C，请注释掉本段
-- -----------------------------------------------------------
UPDATE CharBase SET
    level = 1, vocation = 1, sex = 1, map_id = 1001,
    pos_x = 0, pos_y = 0, pos_z = 0,
    hp = 100, max_hp = 100, mp = 100, max_mp = 100, gold = 1000
WHERE name = 'test001';

UPDATE CharBase SET
    level = 5, vocation = 2, sex = 1, map_id = 1002,
    pos_x = 10, pos_y = 0, pos_z = 8,
    hp = 180, max_hp = 200, mp = 120, max_mp = 150, gold = 5000
WHERE name = 'test002';

UPDATE CharBase SET
    level = 10, vocation = 3, sex = 2, map_id = 2001,
    pos_x = 22, pos_y = 0, pos_z = 16,
    hp = 260, max_hp = 300, mp = 180, max_mp = 220, gold = 12000
WHERE name = 'test003';

INSERT INTO Relation (user_id, friends_json, blacklist_json, guild_id, team_id, `binary`)
VALUES
    (1, '2,3', '', 0, 0, x''),
    (2, '1',   '', 0, 0, x''),
    (3, '1',   '', 0, 0, x'')
ON DUPLICATE KEY UPDATE
    friends_json   = VALUES(friends_json),
    blacklist_json = VALUES(blacklist_json),
    guild_id       = VALUES(guild_id),
    team_id        = VALUES(team_id),
    `binary`       = VALUES(`binary`);

-- -----------------------------------------------------------
-- 【方案 C】按 user_id 批量改（不依赖角色名）
-- -----------------------------------------------------------
/*
UPDATE CharBase
SET gold = 99999, level = 99
WHERE user_id IN (1, 2, 3);
*/

-- -----------------------------------------------------------
-- 改后确认
-- -----------------------------------------------------------
SELECT user_id, name, level, map_id, hp, max_hp, mp, max_mp, gold, update_time
FROM CharBase
WHERE name IN ('test001', 'test002', 'test003')
ORDER BY user_id;
