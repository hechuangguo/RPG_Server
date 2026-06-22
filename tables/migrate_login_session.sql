-- ============================================================
--  RPG — 存量环境补齐 rpg_login.LoginSession 表
--  库名：rpg_login
--  执行：mysql -h HOST -u USER -p rpg_login < tables/migrate_login_session.sql
--  可重复执行（CREATE TABLE IF NOT EXISTS）
--
--  用途：登录成功后写入短期 loginToken；Gateway 鉴权时经 Record/Login 校验并消费。
--  若缺失此表，LoginServer 日志会出现「写入 LoginSession 失败」、客户端「会话写入失败」。
-- ============================================================

CREATE DATABASE IF NOT EXISTS rpg_login DEFAULT CHARACTER SET utf8mb4;

USE rpg_login;

-- 表：LoginSession — Gateway 鉴权票据（LoginServer 写，Login 校验后删除）
CREATE TABLE IF NOT EXISTS LoginSession (
    token       CHAR(64) PRIMARY KEY COMMENT '登录票据（64 字符 hex）',
    accid       BIGINT UNSIGNED NOT NULL COMMENT '账号 ID（关联 GameUser.accid）',
    zone_id     INT UNSIGNED NOT NULL COMMENT '游戏区号',
    game_type   TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '游戏类型',
    expire_time DATETIME NOT NULL COMMENT '过期时间',
    INDEX idx_accid_zone (accid, zone_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
