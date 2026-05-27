-- ============================================================
--  RPG 游戏服务器数据库初始化脚本
--  数据库：rpg_game
--  应用账号：rpg_table / rpg_table（见 create_user_and_db.sql、database.credentials）
--  执行顺序：
--    1) mysql -u root -p < tables/create_user_and_db.sql
--    2) mysql -u root -p < tables/init.sql
--    或 ./tables/setup_database.sh
--    3) 可选：mysql -u rpg_table -prpg_table rpg_game < tables/seed_test_data.sql
--  说明：
--    1) 角色基础数据统一存储于 CharBase（账号与角色基础属性已合并）
--    2) 包裹/技能/状态/任务等功能数据序列化后存入 CharBase.binary
--    3) 本脚本可重复执行（CREATE TABLE IF NOT EXISTS）
-- ============================================================

CREATE DATABASE IF NOT EXISTS rpg_game DEFAULT CHARACTER SET utf8mb4;
USE rpg_game;

-- -----------------------------------------------------------
-- 表：CharBase（角色基础总表）
-- 设计意图：将账号与角色基础属性统一为单表，
--           统一存储角色基础属性与功能序列化二进制数据。
--           SceneServer / SessionServer 的扩展数据由 RecordServer
--           序列化写入 binary 字段。
-- -----------------------------------------------------------
CREATE TABLE IF NOT EXISTS CharBase (
    user_id      INT UNSIGNED AUTO_INCREMENT PRIMARY KEY COMMENT '角色唯一ID，自增主键',
    name         VARCHAR(32) NOT NULL UNIQUE COMMENT '角色名，全局唯一',
    level        INT UNSIGNED DEFAULT 1 COMMENT '角色等级',
    vocation     TINYINT UNSIGNED DEFAULT 0 COMMENT '职业类型（0=无 1=战士 2=法师 3=弓箭手等）',
    sex          TINYINT UNSIGNED DEFAULT 0 COMMENT '性别（0=未知 1=男 2=女）',
    map_id       INT UNSIGNED DEFAULT 1001 COMMENT '当前所在地图ID，默认新手村',
    pos_x        FLOAT DEFAULT 0 COMMENT 'X轴坐标',
    pos_y        FLOAT DEFAULT 0 COMMENT 'Y轴坐标',
    pos_z        FLOAT DEFAULT 0 COMMENT 'Z轴坐标',
    hp           INT UNSIGNED DEFAULT 100 COMMENT '当前生命值',
    max_hp       INT UNSIGNED DEFAULT 100 COMMENT '最大生命值上限',
    mp           INT UNSIGNED DEFAULT 100 COMMENT '当前魔法值/能量值',
    max_mp       INT UNSIGNED DEFAULT 100 COMMENT '最大魔法值/能量值上限',
    gold         BIGINT UNSIGNED DEFAULT 0 COMMENT '拥有金币数量',
    `binary`     MEDIUMBLOB COMMENT '包裹/技能/Buff/任务等功能数据的二进制序列化集合',
    create_time  DATETIME DEFAULT CURRENT_TIMESTAMP COMMENT '角色创建时间',
    update_time  DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '最后更新时间',
    INDEX idx_name (name)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- -----------------------------------------------------------
-- 表：Relation（社会关系 —— SessionServer 直连读写）
-- 设计意图：好友/黑名单/公会/队伍等社交数据，JSON 存储好友与黑名单列表。
-- -----------------------------------------------------------
CREATE TABLE IF NOT EXISTS Relation (
    user_id         INT UNSIGNED PRIMARY KEY COMMENT '用户ID',
    friends_json    TEXT COMMENT '好友ID列表，逗号分隔',
    blacklist_json  TEXT COMMENT '黑名单ID列表，逗号分隔',
    guild_id        BIGINT UNSIGNED DEFAULT 0 COMMENT '公会ID',
    team_id         INT UNSIGNED DEFAULT 0 COMMENT '队伍ID',
    update_time     DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间'
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;


-- -----------------------------------------------------------
-- 表：Friend（好友关系表）
-- 设计意图：存储用户之间的社交关系（好友/黑名单）。
--           采用双向记录方式，A添加B时同时插入两条记录。
--           status 字段区分好友和黑名单两种关系类型。
--           通过 user_id 关联 CharBase.user_id。
-- -----------------------------------------------------------
CREATE TABLE IF NOT EXISTS Friend (
    id          BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY COMMENT '主键ID，自增',
    user_id     INT UNSIGNED NOT NULL COMMENT '发起方角色ID，关联 CharBase.user_id',
    friend_id   INT UNSIGNED NOT NULL COMMENT '目标角色ID，关联 CharBase.user_id',
    status      TINYINT DEFAULT 1 COMMENT '关系状态：1=好友 2=黑名单',
    create_time DATETIME DEFAULT CURRENT_TIMESTAMP COMMENT '关系建立时间',
    UNIQUE KEY uk_user_friend (user_id, friend_id),
    INDEX idx_user_id (user_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- -----------------------------------------------------------
-- 表：Mail（离线邮件表）
-- 设计意图：存储发给用户的离线邮件，包括系统公告和玩家间邮件。
--           支持附件（以JSON格式存储物品数据），带过期时间和已读状态。
--           用户上线后检查并收取未读邮件。通过 to_user_id 关联 CharBase。
-- -----------------------------------------------------------
CREATE TABLE IF NOT EXISTS Mail (
    id          BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY COMMENT '主键ID，自增',
    to_user_id  INT UNSIGNED NOT NULL COMMENT '收件人角色ID，关联 CharBase.user_id',
    from_name   VARCHAR(32) DEFAULT '系统' COMMENT '发件人显示名（默认"系统"表示系统邮件）',
    subject     VARCHAR(64) COMMENT '邮件标题',
    content     TEXT COMMENT '邮件正文内容',
    attachment  TEXT COMMENT '附件数据JSON（物品/货币等）',
    is_read     TINYINT DEFAULT 0 COMMENT '是否已读：0=未读 1=已读',
    send_time   DATETIME DEFAULT CURRENT_TIMESTAMP COMMENT '发送时间',
    expire_time DATETIME DEFAULT NULL COMMENT '过期时间（NULL表示永不过期）',
    INDEX idx_to_user (to_user_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- -----------------------------------------------------------
-- 表：MapInfo（地图存档表）
-- 设计意图：保存用户在特定地图（如副本/场景）的独立进度数据。
--           以JSON格式存储地图相关的存档信息（副本进度、探索度、
--           触发事件状态等）。每个用户在同一张地图上只有一条存档记录。
--           通过 user_id 关联 CharBase.user_id。
-- -----------------------------------------------------------
CREATE TABLE IF NOT EXISTS MapInfo (
    id          BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY COMMENT '主键ID，自增',
    user_id     INT UNSIGNED NOT NULL COMMENT '所属角色ID，关联 CharBase.user_id',
    map_id      INT UNSIGNED NOT NULL COMMENT '地图ID',
    archive_data TEXT COMMENT 'JSON格式的地图存档数据（副本进度/探索状态等）',
    update_time DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '最后更新时间',
    UNIQUE KEY uk_user_map (user_id, map_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- -------------------------------------------------------
-- 测试种子数据已拆分至 seed_test_data.sql（开发环境按需执行）
-- -------------------------------------------------------
