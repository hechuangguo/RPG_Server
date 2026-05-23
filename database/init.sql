-- ============================================================
--  RPG 游戏服务器数据库初始化脚本
--  数据库：rpg_game
-- ============================================================

CREATE DATABASE IF NOT EXISTS rpg_game DEFAULT CHARACTER SET utf8mb4;
USE rpg_game;

-- 账号表
CREATE TABLE IF NOT EXISTS t_account (
    id          BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    account     VARCHAR(64) NOT NULL UNIQUE COMMENT '账号',
    password    VARCHAR(64) NOT NULL COMMENT 'MD5密码',
    role_id     BIGINT UNSIGNED DEFAULT 0 COMMENT '关联角色ID',
    create_time DATETIME DEFAULT CURRENT_TIMESTAMP,
    last_login  DATETIME DEFAULT NULL,
    INDEX idx_account (account)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 角色表
CREATE TABLE IF NOT EXISTS t_role (
    role_id     BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    account_id  BIGINT UNSIGNED NOT NULL COMMENT '账号ID',
    name        VARCHAR(32) NOT NULL UNIQUE COMMENT '角色名',
    level       INT UNSIGNED DEFAULT 1,
    vocation    TINYINT UNSIGNED DEFAULT 0 COMMENT '职业',
    sex         TINYINT UNSIGNED DEFAULT 0 COMMENT '性别',
    map_id      INT UNSIGNED DEFAULT 1001 COMMENT '当前地图',
    pos_x       FLOAT DEFAULT 0,
    pos_y       FLOAT DEFAULT 0,
    pos_z       FLOAT DEFAULT 0,
    hp          INT UNSIGNED DEFAULT 100,
    max_hp      INT UNSIGNED DEFAULT 100,
    mp          INT UNSIGNED DEFAULT 100,
    max_mp      INT UNSIGNED DEFAULT 100,
    gold        BIGINT UNSIGNED DEFAULT 0,
    exp         BIGINT UNSIGNED DEFAULT 0,
    create_time DATETIME DEFAULT CURRENT_TIMESTAMP,
    update_time DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    INDEX idx_account_id (account_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 角色背包
CREATE TABLE IF NOT EXISTS t_bag (
    id          BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    role_id     BIGINT UNSIGNED NOT NULL,
    slot        INT UNSIGNED NOT NULL COMMENT '格子索引',
    item_id     INT UNSIGNED NOT NULL,
    count       INT UNSIGNED DEFAULT 1,
    extra_data  TEXT COMMENT '附魔/强化等扩展JSON',
    UNIQUE KEY uk_role_slot (role_id, slot),
    INDEX idx_role_id (role_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 角色技能
CREATE TABLE IF NOT EXISTS t_skill (
    id          BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    role_id     BIGINT UNSIGNED NOT NULL,
    skill_id    INT UNSIGNED NOT NULL,
    skill_level INT UNSIGNED DEFAULT 1,
    INDEX idx_role_id (role_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 任务进度
CREATE TABLE IF NOT EXISTS t_quest (
    id          BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    role_id     BIGINT UNSIGNED NOT NULL,
    quest_id    INT UNSIGNED NOT NULL,
    status      TINYINT DEFAULT 0 COMMENT '0=进行中 1=完成 2=已领奖',
    progress    INT UNSIGNED DEFAULT 0,
    accept_time DATETIME DEFAULT CURRENT_TIMESTAMP,
    done_time   DATETIME DEFAULT NULL,
    UNIQUE KEY uk_role_quest (role_id, quest_id),
    INDEX idx_role_id (role_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 好友关系
CREATE TABLE IF NOT EXISTS t_friend (
    id          BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    role_id     BIGINT UNSIGNED NOT NULL,
    friend_id   BIGINT UNSIGNED NOT NULL,
    status      TINYINT DEFAULT 1 COMMENT '1=好友 2=黑名单',
    create_time DATETIME DEFAULT CURRENT_TIMESTAMP,
    UNIQUE KEY uk_role_friend (role_id, friend_id),
    INDEX idx_role_id (role_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 离线邮件
CREATE TABLE IF NOT EXISTS t_mail (
    id          BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    to_role_id  BIGINT UNSIGNED NOT NULL,
    from_name   VARCHAR(32) DEFAULT '系统',
    subject     VARCHAR(64),
    content     TEXT,
    attachment  TEXT COMMENT '附件JSON',
    is_read     TINYINT DEFAULT 0,
    send_time   DATETIME DEFAULT CURRENT_TIMESTAMP,
    expire_time DATETIME DEFAULT NULL,
    INDEX idx_to_role (to_role_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 地图存档（副本/场景独立进度）
CREATE TABLE IF NOT EXISTS t_map_archive (
    id          BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    role_id     BIGINT UNSIGNED NOT NULL,
    map_id      INT UNSIGNED NOT NULL,
    archive_data TEXT COMMENT 'JSON格式的地图存档',
    update_time DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    UNIQUE KEY uk_role_map (role_id, map_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- -------------------------------------------------------
-- 初始测试账号（密码均为 md5("123456")）
-- -------------------------------------------------------
INSERT IGNORE INTO t_account (account, password) VALUES
    ('test001', MD5('123456')),
    ('test002', MD5('123456')),
    ('test003', MD5('123456'));
