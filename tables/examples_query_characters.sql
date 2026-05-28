-- ============================================================
--  示例：只查角色（CharBase）
--  数据库：rpg_game
--  执行：mysql -h 127.0.0.1 -u rpg_table -prpg_table rpg_game < tables/examples_query_characters.sql
--        或在 mysql 客户端内：SOURCE /home/hechuangguo/RPG/tables/examples_query_characters.sql;
--  前提：已执行 init.sql；测试数据见 seed_test_data.sql（test001~test003）
-- ============================================================

USE rpg_game;

-- -----------------------------------------------------------
-- 1. 列出全部角色（常用字段，不含 binary）
-- -----------------------------------------------------------
SELECT
    user_id,
    name,
    level,
    vocation,
    sex,
    map_id,
    pos_x, pos_y, pos_z,
    hp, max_hp, mp, max_mp,
    gold,
    create_time,
    update_time
FROM CharBase
ORDER BY user_id;

-- -----------------------------------------------------------
-- 2. 按角色名查询（精确匹配）
-- -----------------------------------------------------------
SELECT user_id, name, level, map_id, gold
FROM CharBase
WHERE name = 'test001';

-- -----------------------------------------------------------
-- 3. 按角色名模糊查询
-- -----------------------------------------------------------
SELECT user_id, name, level, gold
FROM CharBase
WHERE name LIKE 'test%'
ORDER BY name;

-- -----------------------------------------------------------
-- 4. 按 user_id 查询单角色
-- -----------------------------------------------------------
SELECT *
FROM CharBase
WHERE user_id = 1;

-- -----------------------------------------------------------
-- 5. 按等级 / 地图筛选
-- -----------------------------------------------------------
SELECT user_id, name, level, map_id, gold
FROM CharBase
WHERE level >= 5
  AND map_id = 1002;

-- -----------------------------------------------------------
-- 6. 统计：角色总数、平均等级、总金币
-- -----------------------------------------------------------
SELECT
    COUNT(*)              AS role_count,
    AVG(level)            AS avg_level,
    SUM(gold)             AS total_gold
FROM CharBase;

-- -----------------------------------------------------------
-- 7. 查看 binary 字段大小（不展开内容，避免乱码）
-- -----------------------------------------------------------
SELECT
    user_id,
    name,
    OCTET_LENGTH(`binary`) AS binary_bytes
FROM CharBase
ORDER BY user_id;

-- -----------------------------------------------------------
-- 8. 角色 + 社交 Relation（左连接，无 Relation 时仍显示角色）
-- -----------------------------------------------------------
SELECT
    c.user_id,
    c.name,
    c.level,
    c.gold,
    r.friends_json,
    r.guild_id,
    r.team_id,
    OCTET_LENGTH(r.`binary`) AS relation_binary_bytes
FROM CharBase c
LEFT JOIN Relation r ON r.user_id = c.user_id
ORDER BY c.user_id;
