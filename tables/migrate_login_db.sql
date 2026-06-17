-- ============================================================
--  RPG — 存量环境迁移：将 GameUser、ZoneInfo 从 rpg_game 迁至 rpg_login
--  适用：升级前 rpg_game 含 GameUser/ZoneInfo 的旧库
--  新环境请直接执行 init.sql，无需本脚本
--  执行前请备份数据库；可重复执行（幂等）
--  执行：mysql -u root -p < tables/migrate_login_db.sql
-- ============================================================

CREATE DATABASE IF NOT EXISTS rpg_login DEFAULT CHARACTER SET utf8mb4;

-- 迁移 GameUser
SET @hasGameUser := (
    SELECT COUNT(*) FROM information_schema.TABLES
    WHERE TABLE_SCHEMA = 'rpg_game' AND TABLE_NAME = 'GameUser'
);
SET @sql := IF(@hasGameUser > 0,
    'CREATE TABLE IF NOT EXISTS rpg_login.GameUser LIKE rpg_game.GameUser',
    'SELECT 1');
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @sql := IF(@hasGameUser > 0,
    'INSERT IGNORE INTO rpg_login.GameUser SELECT * FROM rpg_game.GameUser',
    'SELECT 1');
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @sql := IF(@hasGameUser > 0,
    'DROP TABLE IF EXISTS rpg_game.GameUser',
    'SELECT 1');
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

-- 迁移 ZoneInfo
SET @hasZoneInfo := (
    SELECT COUNT(*) FROM information_schema.TABLES
    WHERE TABLE_SCHEMA = 'rpg_game' AND TABLE_NAME = 'ZoneInfo'
);
SET @sql := IF(@hasZoneInfo > 0,
    'CREATE TABLE IF NOT EXISTS rpg_login.ZoneInfo LIKE rpg_game.ZoneInfo',
    'SELECT 1');
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @sql := IF(@hasZoneInfo > 0,
    'INSERT IGNORE INTO rpg_login.ZoneInfo SELECT * FROM rpg_game.ZoneInfo',
    'SELECT 1');
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @sql := IF(@hasZoneInfo > 0,
    'DROP TABLE IF EXISTS rpg_game.ZoneInfo',
    'SELECT 1');
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;
