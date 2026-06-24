-- ============================================================
--  RPG — CharBase 角色模型 model_id 存量迁移
--  执行：mysql -u root -p < tables/alter_character_model.sql
--  可重复执行（列已存在则跳过）；存量角色默认 model_id=1
-- ============================================================

USE rpg_game;

SET @hasModelId := (
    SELECT COUNT(*) FROM information_schema.COLUMNS
    WHERE TABLE_SCHEMA = 'rpg_game' AND TABLE_NAME = 'CharBase' AND COLUMN_NAME = 'model_id'
);
SET @sql := IF(@hasModelId = 0,
    'ALTER TABLE CharBase ADD COLUMN model_id TINYINT UNSIGNED NOT NULL DEFAULT 1 COMMENT ''角色模型ID（1=男大 2=男小 3=女大 4=女小）'' AFTER sex',
    'SELECT 1');
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;
