-- ============================================================
--  RPG 游戏服务器数据库初始化脚本
--  数据库：rpg_game
--  说明：本脚本创建游戏所需的所有数据表，包含账号、用户、
--        背包、技能、任务、好友、邮件、地图存档等模块。
-- ============================================================

CREATE DATABASE IF NOT EXISTS rpg_game DEFAULT CHARACTER SET utf8mb4;
USE rpg_game;

-- -----------------------------------------------------------
-- 表：t_account（账号表）
-- 设计意图：存储玩家的登录账号信息，每个账号可绑定一个用户。
--           账号登录后通过关联字段找到对应的用户数据进行加载。
-- -----------------------------------------------------------
CREATE TABLE IF NOT EXISTS t_account (
    id          BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY COMMENT '主键ID，自增',
    account     VARCHAR(64) NOT NULL UNIQUE COMMENT '登录账号名，唯一',
    password    VARCHAR(64) NOT NULL COMMENT 'MD5加密后的密码',
    user_id     BIGINT UNSIGNED DEFAULT 0 COMMENT '关联的用户ID（对应 t_user.user_id）',
    create_time DATETIME DEFAULT CURRENT_TIMESTAMP COMMENT '账号创建时间',
    last_login  DATETIME DEFAULT NULL COMMENT '最后登录时间',
    INDEX idx_account (account)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- -----------------------------------------------------------
-- 表：t_user（用户表）
-- 设计意图：存储游戏用户的属性数据，包括基础信息（名称/职业/性别）、
--           战斗属性（血量/魔法/经验）、位置信息（当前地图/坐标）等。
--           每条记录代表一个游戏内的用户，通过 account_id 关联到 t_account。
-- -----------------------------------------------------------
CREATE TABLE IF NOT EXISTS t_user (
    user_id     INT UNSIGNED AUTO_INCREMENT PRIMARY KEY COMMENT '用户唯一标识，自增主键',
    account_id  BIGINT UNSIGNED NOT NULL COMMENT '所属账号ID，关联 t_account.id',
    name        VARCHAR(32) NOT NULL UNIQUE COMMENT '用户名，全局唯一',
    level       INT UNSIGNED DEFAULT 1 COMMENT '用户等级，初始为1',
    vocation    TINYINT UNSIGNED DEFAULT 0 COMMENT '职业类型（0=无 1=战士 2=法师 3=弓箭手等）',
    sex         TINYINT UNSIGNED DEFAULT 0 COMMENT '性别（0=未知 1=男 2=女）',
    map_id      INT UNSIGNED DEFAULT 1001 COMMENT '当前所在地图ID，默认新手村',
    pos_x       FLOAT DEFAULT 0 COMMENT 'X轴坐标',
    pos_y       FLOAT DEFAULT 0 COMMENT 'Y轴坐标',
    pos_z       FLOAT DEFAULT 0 COMMENT 'Z轴坐标',
    hp          INT UNSIGNED DEFAULT 100 COMMENT '当前生命值',
    max_hp      INT UNSIGNED DEFAULT 100 COMMENT '最大生命值上限',
    mp          INT UNSIGNED DEFAULT 100 COMMENT '当前魔法值/能量值',
    max_mp      INT UNSIGNED DEFAULT 100 COMMENT '最大魔法值/能量值上限',
    gold        BIGINT UNSIGNED DEFAULT 0 COMMENT '拥有金币数量',
    exp         BIGINT UNSIGNED DEFAULT 0 COMMENT '当前经验值',
    create_time DATETIME DEFAULT CURRENT_TIMESTAMP COMMENT '用户创建时间',
    update_time DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '最后更新时间',
    INDEX idx_account_id (account_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- -----------------------------------------------------------
-- 表：t_charbase（角色在线基础属性 —— Scene 经 RecordServer 存档）
-- 设计意图：存储角色当前等级、坐标、HP/MP 等在线可变属性，
--           与 t_user 解耦，由 RecordServer 统一读写。
-- -----------------------------------------------------------
CREATE TABLE IF NOT EXISTS t_charbase (
    user_id     INT UNSIGNED PRIMARY KEY COMMENT '用户ID，关联 t_user.user_id',
    name        VARCHAR(32) NOT NULL DEFAULT '' COMMENT '角色名',
    level       INT UNSIGNED DEFAULT 1 COMMENT '等级',
    vocation    TINYINT UNSIGNED DEFAULT 0 COMMENT '职业',
    sex         TINYINT UNSIGNED DEFAULT 0 COMMENT '性别',
    map_id      INT UNSIGNED DEFAULT 1001 COMMENT '当前地图',
    pos_x       FLOAT DEFAULT 0 COMMENT 'X坐标',
    pos_y       FLOAT DEFAULT 0 COMMENT 'Y坐标',
    pos_z       FLOAT DEFAULT 0 COMMENT 'Z坐标',
    hp          INT UNSIGNED DEFAULT 100 COMMENT '当前HP',
    max_hp      INT UNSIGNED DEFAULT 100 COMMENT '最大HP',
    mp          INT UNSIGNED DEFAULT 100 COMMENT '当前MP',
    max_mp      INT UNSIGNED DEFAULT 100 COMMENT '最大MP',
    gold        BIGINT UNSIGNED DEFAULT 0 COMMENT '金币',
    update_time DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间'
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- -----------------------------------------------------------
-- 表：t_relation（社会关系 —— SessionServer 直连读写）
-- 设计意图：好友/黑名单/公会/队伍等社交数据，JSON 存储好友与黑名单列表。
-- -----------------------------------------------------------
CREATE TABLE IF NOT EXISTS t_relation (
    user_id         INT UNSIGNED PRIMARY KEY COMMENT '用户ID',
    friends_json    TEXT COMMENT '好友ID列表，逗号分隔',
    blacklist_json  TEXT COMMENT '黑名单ID列表，逗号分隔',
    guild_id        BIGINT UNSIGNED DEFAULT 0 COMMENT '公会ID',
    team_id         INT UNSIGNED DEFAULT 0 COMMENT '队伍ID',
    update_time     DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间'
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- -----------------------------------------------------------
-- 表：t_bag（用户背包表）
-- 设计意图：存储用户背包中的物品数据，以格子（slot）为单位管理物品。
--           每个用户有固定数量的背包格子，每个格子存放一个物品及其数量，
--           支持扩展数据（附魔、强化等属性以JSON格式存储）。
--           通过 user_id 关联 t_user 表。
-- -----------------------------------------------------------
CREATE TABLE IF NOT EXISTS t_bag (
    id          BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY COMMENT '主键ID，自增',
    user_id     INT UNSIGNED NOT NULL COMMENT '所属用户ID，关联 t_user.user_id',
    slot        INT UNSIGNED NOT NULL COMMENT '背包格子索引（从0开始）',
    item_id     INT UNSIGNED NOT NULL COMMENT '物品模板ID，对应物品配置表',
    count       INT UNSIGNED DEFAULT 1 COMMENT '该格子的物品堆叠数量',
    extra_data  TEXT COMMENT '扩展数据JSON（附魔/强化/镶嵌等附加属性）',
    UNIQUE KEY uk_user_slot (user_id, slot),
    INDEX idx_user_id (user_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- -----------------------------------------------------------
-- 表：t_skill（用户技能表）
-- 设计意图：存储用户已学习的技能列表及技能等级。
--           用户可通过升级或完成任务解锁新技能，技能等级影响效果数值。
--           通过 user_id 关联 t_user 表。
-- -----------------------------------------------------------
CREATE TABLE IF NOT EXISTS t_skill (
    id          BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY COMMENT '主键ID，自增',
    user_id     INT UNSIGNED NOT NULL COMMENT '所属用户ID，关联 t_user.user_id',
    skill_id    INT UNSIGNED NOT NULL COMMENT '技能模板ID，对应技能配置表',
    skill_level INT UNSIGNED DEFAULT 1 COMMENT '技能等级，等级越高效果越强',
    INDEX idx_user_id (user_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- -----------------------------------------------------------
-- 表：t_quest（任务进度表）
-- 设计意图：存储用户已接受的任务进度状态。
--           记录任务的接受时间、完成时间、当前进度等，支持进行中/
--           完成/已领奖三种状态。一个用户对同一任务只有一条记录。
--           通过 user_id 关联 t_user 表。
-- -----------------------------------------------------------
CREATE TABLE IF NOT EXISTS t_quest (
    id          BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY COMMENT '主键ID，自增',
    user_id     INT UNSIGNED NOT NULL COMMENT '所属用户ID，关联 t_user.user_id',
    quest_id    INT UNSIGNED NOT NULL COMMENT '任务模板ID，对应任务配置表',
    status      TINYINT DEFAULT 0 COMMENT '任务状态：0=进行中 1=已完成 2=已领奖',
    progress    INT UNSIGNED DEFAULT 0 COMMENT '任务进度值（如击杀数量/收集数量等）',
    accept_time DATETIME DEFAULT CURRENT_TIMESTAMP COMMENT '任务接受时间',
    done_time   DATETIME DEFAULT NULL COMMENT '任务完成时间',
    UNIQUE KEY uk_user_quest (user_id, quest_id),
    INDEX idx_user_id (user_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- -----------------------------------------------------------
-- 表：t_friend（好友关系表）
-- 设计意图：存储用户之间的社交关系（好友/黑名单）。
--           采用双向记录方式，A添加B时同时插入两条记录。
--           status 字段区分好友和黑名单两种关系类型。
--           通过 user_id 关联 t_user 表。
-- -----------------------------------------------------------
CREATE TABLE IF NOT EXISTS t_friend (
    id          BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY COMMENT '主键ID，自增',
    user_id     INT UNSIGNED NOT NULL COMMENT '发起方的用户ID，关联 t_user.user_id',
    friend_id   INT UNSIGNED NOT NULL COMMENT '目标用户ID，关联 t_user.user_id',
    status      TINYINT DEFAULT 1 COMMENT '关系状态：1=好友 2=黑名单',
    create_time DATETIME DEFAULT CURRENT_TIMESTAMP COMMENT '关系建立时间',
    UNIQUE KEY uk_user_friend (user_id, friend_id),
    INDEX idx_user_id (user_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- -----------------------------------------------------------
-- 表：t_mail（离线邮件表）
-- 设计意图：存储发给用户的离线邮件，包括系统公告和玩家间邮件。
--           支持附件（以JSON格式存储物品数据），带过期时间和已读状态。
--           用户上线后检查并收取未读邮件。通过 to_user_id 关联 t_user。
-- -----------------------------------------------------------
CREATE TABLE IF NOT EXISTS t_mail (
    id          BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY COMMENT '主键ID，自增',
    to_user_id  INT UNSIGNED NOT NULL COMMENT '收件人用户ID，关联 t_user.user_id',
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
-- 表：t_map_archive（地图存档表）
-- 设计意图：保存用户在特定地图（如副本/场景）的独立进度数据。
--           以JSON格式存储地图相关的存档信息（副本进度、探索度、
--           触发事件状态等）。每个用户在同一张地图上只有一条存档记录。
--           通过 user_id 关联 t_user 表。
-- -----------------------------------------------------------
CREATE TABLE IF NOT EXISTS t_map_archive (
    id          BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY COMMENT '主键ID，自增',
    user_id     INT UNSIGNED NOT NULL COMMENT '所属用户ID，关联 t_user.user_id',
    map_id      INT UNSIGNED NOT NULL COMMENT '地图ID',
    archive_data TEXT COMMENT 'JSON格式的地图存档数据（副本进度/探索状态等）',
    update_time DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '最后更新时间',
    UNIQUE KEY uk_user_map (user_id, map_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- -------------------------------------------------------
-- 初始测试账号（密码均为 md5("123456")）
-- 用于开发测试阶段验证登录流程
-- -------------------------------------------------------
INSERT IGNORE INTO t_account (account, password) VALUES
    ('test001', MD5('123456')),
    ('test002', MD5('123456')),
    ('test003', MD5('123456'));

-- 测试角色 charbase（user_id=1，需与 t_account.user_id 绑定后生效）
INSERT IGNORE INTO t_charbase (user_id, name, level, map_id, hp, max_hp, mp, max_mp, gold)
VALUES (1, 'test001', 1, 1001, 100, 100, 100, 100, 0);

INSERT IGNORE INTO t_relation (user_id, friends_json, blacklist_json, guild_id, team_id)
VALUES (1, '', '', 0, 0);

UPDATE t_account SET user_id=1 WHERE account='test001' AND user_id=0;
