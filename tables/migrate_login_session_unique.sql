-- ============================================================
--  RPG — LoginSession 唯一约束（每账号每区仅一条有效票据）
--  库名：rpg_login
--  执行：mysql -h HOST -u USER -p < tables/migrate_login_session_unique.sql
--  可重复执行
-- ============================================================

USE rpg_login;

DELETE t1 FROM LoginSession t1
INNER JOIN LoginSession t2
    ON t1.accid = t2.accid AND t1.zone_id = t2.zone_id AND t1.token < t2.token;

SET @hasUk := (
    SELECT COUNT(*) FROM information_schema.STATISTICS
    WHERE TABLE_SCHEMA = 'rpg_login' AND TABLE_NAME = 'LoginSession' AND INDEX_NAME = 'uk_accid_zone'
);
SET @sql := IF(@hasUk = 0,
    'ALTER TABLE LoginSession ADD UNIQUE KEY uk_accid_zone (accid, zone_id)',
    'SELECT 1');
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;
