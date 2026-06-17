-- ============================================================
--  RPG — 登录进场景流程存量迁移
--  执行：mysql -u root -p < tables/alter_login_flow.sql
--  可重复执行（列/表已存在则跳过）
-- ============================================================

CREATE DATABASE IF NOT EXISTS rpg_login DEFAULT CHARACTER SET utf8mb4;

USE rpg_login;

CREATE TABLE IF NOT EXISTS LoginSession (
    token       CHAR(64) PRIMARY KEY COMMENT '登录票据',
    accid       BIGINT UNSIGNED NOT NULL COMMENT '账号 ID',
    zone_id     INT UNSIGNED NOT NULL COMMENT '游戏区号',
    game_type   TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '游戏类型',
    expire_time DATETIME NOT NULL COMMENT '过期时间',
    INDEX idx_accid_zone (accid, zone_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

USE rpg_game;

SET @hasAccid := (
    SELECT COUNT(*) FROM information_schema.COLUMNS
    WHERE TABLE_SCHEMA = 'rpg_game' AND TABLE_NAME = 'CharBase' AND COLUMN_NAME = 'accid'
);
SET @sql := IF(@hasAccid = 0,
    'ALTER TABLE CharBase ADD COLUMN accid BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT ''所属账号 ID'' AFTER user_id',
    'SELECT 1');
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @hasZone := (
    SELECT COUNT(*) FROM information_schema.COLUMNS
    WHERE TABLE_SCHEMA = 'rpg_game' AND TABLE_NAME = 'CharBase' AND COLUMN_NAME = 'gamezone'
);
SET @sql := IF(@hasZone = 0,
    'ALTER TABLE CharBase ADD COLUMN gamezone INT UNSIGNED NOT NULL DEFAULT 0 COMMENT ''所属游戏区号'' AFTER accid',
    'SELECT 1');
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @hasIdx := (
    SELECT COUNT(*) FROM information_schema.STATISTICS
    WHERE TABLE_SCHEMA = 'rpg_game' AND TABLE_NAME = 'CharBase' AND INDEX_NAME = 'idx_accid_zone'
);
SET @sql := IF(@hasIdx = 0,
    'ALTER TABLE CharBase ADD INDEX idx_accid_zone (accid, gamezone)',
    'SELECT 1');
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;
