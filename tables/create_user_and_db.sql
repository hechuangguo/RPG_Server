-- ============================================================
--  RPG — 创建数据库与应用账号（须使用 root 或具备 CREATE USER 权限的账号执行）
--  库名：rpg_game
--  用户：rpg_table / rpg_table
--  执行：mysql -u root -p < tables/create_user_and_db.sql
--  可重复执行
-- ============================================================

CREATE DATABASE IF NOT EXISTS rpg_game
    DEFAULT CHARACTER SET utf8mb4
    COLLATE utf8mb4_unicode_ci;

-- MySQL 8：使用 mysql_native_password，兼容 Database Client 等旧版客户端
CREATE USER IF NOT EXISTS 'rpg_table'@'%' IDENTIFIED WITH mysql_native_password BY 'rpg_table';
CREATE USER IF NOT EXISTS 'rpg_table'@'localhost' IDENTIFIED WITH mysql_native_password BY 'rpg_table';
CREATE USER IF NOT EXISTS 'rpg_table'@'127.0.0.1' IDENTIFIED WITH mysql_native_password BY 'rpg_table';

ALTER USER 'rpg_table'@'%' IDENTIFIED WITH mysql_native_password BY 'rpg_table';
ALTER USER 'rpg_table'@'localhost' IDENTIFIED WITH mysql_native_password BY 'rpg_table';
ALTER USER 'rpg_table'@'127.0.0.1' IDENTIFIED WITH mysql_native_password BY 'rpg_table';

GRANT ALL PRIVILEGES ON rpg_game.* TO 'rpg_table'@'%';
GRANT ALL PRIVILEGES ON rpg_game.* TO 'rpg_table'@'localhost';
GRANT ALL PRIVILEGES ON rpg_game.* TO 'rpg_table'@'127.0.0.1';

FLUSH PRIVILEGES;
