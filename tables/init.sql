-- ============================================================
--  RPG 游戏服务器数据库初始化脚本
--  三库：rpg_login（登录服）、rpg_game（游戏区）、rpg_global（全局服）
--  应用账号：rpg_table / rpg_table（见 create_user_and_db.sql、database.credentials）
--  执行顺序：
--    1) mysql -u root -p < tables/create_user_and_db.sql
--    2) mysql -u root -p < tables/init.sql
--    或 ./tables/setup_database.sh
--    3) 可选：mysql -u rpg_table -prpg_table rpg_game < tables/seed_test_data.sql
--  存量升级（rpg_game 含 GameUser/ZoneInfo）：migrate_login_db.sql
--  说明：本脚本可重复执行（CREATE TABLE IF NOT EXISTS）
-- ============================================================

-- ============================================================
-- Part A: rpg_login — LoginServer 专用（账号与区服入口）
-- ============================================================

CREATE DATABASE IF NOT EXISTS rpg_login DEFAULT CHARACTER SET utf8mb4;
USE rpg_login;

-- -----------------------------------------------------------
-- 表：GameUser（账号主表 —— LoginServer 注册/登录真源）
-- 设计意图：独立于 CharBase 的账号体系，保存账号、密码哈希、所属区服与绑定角色。
--           注册成功仅创建账号行并分配 accid，user_id 初始为 0，待后续创角后回填。
--           密码字段存 bcrypt(hex(SHA-256(UTF-8 明文)))，wire 禁止明文；生成见 scripts/gen_password_digest.sh
-- -----------------------------------------------------------
CREATE TABLE IF NOT EXISTS GameUser (
    accid         BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY COMMENT '账号ID，自增主键',
    account       VARCHAR(64) NOT NULL COMMENT '账号名，登录唯一键',
    password_hash VARCHAR(128) NOT NULL COMMENT 'bcrypt(hex(SHA-256(密码)))，wire 传 32B digest',
    user_id       INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '绑定角色ID（0表示尚未创角）',
    gamezone      INT UNSIGNED NOT NULL COMMENT '注册时选择的游戏区号',
    create_time   DATETIME DEFAULT CURRENT_TIMESTAMP COMMENT '账号创建时间',
    update_time   DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '最后更新时间',
    UNIQUE KEY uk_account (account),
    INDEX idx_user_id (user_id),
    INDEX idx_gamezone (gamezone)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- -----------------------------------------------------------
-- 表：ZoneInfo（游戏区入口参考表 —— 多游戏公用）
-- 设计意图：登记各游戏类型下的可登录区服入口（IP/Super 端口/维护开关）。
--           game_type 区分游戏产品；zone_id 区分同产品下的游戏区号。
--           LoginServer 实际读取 LoginServer/serverlist.xml（本表仅作 DB 参考/种子）。
--           Gateway gatewayServerId 建议与 zone_id 对齐（同 game_type 下）。
-- -----------------------------------------------------------
CREATE TABLE IF NOT EXISTS ZoneInfo (
    zone_id      INT UNSIGNED NOT NULL COMMENT '游戏区号（同 game_type 下唯一，如 1=一区）',
    game_type    TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '游戏类型（0=当前 RPG，其它值预留给未来游戏）',
    name         VARCHAR(32) NOT NULL DEFAULT '' COMMENT '区服显示名',
    ip           VARCHAR(64) NOT NULL DEFAULT '127.0.0.1' COMMENT '入口 IP（VIP 或对外地址）',
    super_port   SMALLINT UNSIGNED NOT NULL DEFAULT 9000 COMMENT 'SuperServer 端口',
    enabled      TINYINT UNSIGNED NOT NULL DEFAULT 1 COMMENT '是否可用：1=可登录 0=维护',
    update_time  DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '最后更新时间',
    PRIMARY KEY (game_type, zone_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

INSERT INTO ZoneInfo (zone_id, game_type, name, ip, super_port, enabled) VALUES
    (1, 0, 'RPG一区', '127.0.0.1', 9000, 1)
ON DUPLICATE KEY UPDATE
    name=VALUES(name), ip=VALUES(ip),
    super_port=VALUES(super_port), enabled=VALUES(enabled);

-- -----------------------------------------------------------
-- 表：LoginSession（Gateway 鉴权票据 —— LoginServer 写入，Record 跨库校验）
-- 设计意图：登录成功后生成短期一次性 token；Gateway 经 Record 校验后消费。
-- -----------------------------------------------------------
CREATE TABLE IF NOT EXISTS LoginSession (
    token       CHAR(64) PRIMARY KEY COMMENT '登录票据（64 字符 hex）',
    accid       BIGINT UNSIGNED NOT NULL COMMENT '账号 ID',
    zone_id     INT UNSIGNED NOT NULL COMMENT '游戏区号',
    game_type   TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '游戏类型',
    expire_time DATETIME NOT NULL COMMENT '过期时间',
    INDEX idx_accid_zone (accid, zone_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ============================================================
-- Part B: rpg_game — 游戏区专用（Super/Record/Session）
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
    accid        BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '所属账号 ID（关联 rpg_login.GameUser.accid）',
    gamezone     INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '所属游戏区号',
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
    INDEX idx_name (name),
    INDEX idx_accid_zone (accid, gamezone)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- -----------------------------------------------------------
-- 表：Relation（社会关系 —— RecordServer 读写，Session 经 REC_RELATION_* 访问）
-- 设计意图：好友/黑名单/公会/队伍；friends_json/blacklist_json 存 ID 列表；
--           binary 存社交扩展二进制（申请列表、缓存等，由 SessionServer 序列化后经 Record 落库）。
-- -----------------------------------------------------------
CREATE TABLE IF NOT EXISTS Relation (
    user_id         INT UNSIGNED PRIMARY KEY COMMENT '用户ID',
    friends_json    TEXT COMMENT '好友ID列表，逗号分隔',
    blacklist_json  TEXT COMMENT '黑名单ID列表，逗号分隔',
    guild_id        BIGINT UNSIGNED DEFAULT 0 COMMENT '公会ID',
    team_id         INT UNSIGNED DEFAULT 0 COMMENT '队伍ID',
    `binary`        MEDIUMBLOB COMMENT '社交扩展数据二进制序列化（申请/缓存等）',
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

-- -----------------------------------------------------------
-- 表：ServerList（游戏区内集群拓扑表 —— SuperServer 启动只读加载）
-- 设计意图：集中登记游戏区内服务器（Super/Session/Record/AOI/Scene/Gateway）的
--           编号/类型/监听地址/名称。SuperServer 启动时直连 MySQL 只读此表并缓存，
--           子服启动拉取后绑定自身端口、连接区内对端。
--           Logger/Global/Zone 为可选外联服，不在此表登记，由 loginserverlist.xml 配置。
-- 注意：server_type 仅登记 0=Super, 1=Session, 2=Record, 3=AOI, 4=Scene, 5=Gateway。
-- 本脚本可重复执行：先 INSERT，再 ON DUPLICATE KEY UPDATE 保持幂等。
-- -----------------------------------------------------------
CREATE TABLE IF NOT EXISTS ServerList (
    server_id    INT UNSIGNED NOT NULL COMMENT '服务器实例编号（同类型下唯一，从 1 起）',
    server_type  TINYINT UNSIGNED NOT NULL COMMENT '服务器类型（区内：0 Super,1 Session,2 Record,3 AOI,4 Scene,5 Gateway）',
    ip           VARCHAR(64) NOT NULL DEFAULT '127.0.0.1' COMMENT '监听 IP',
    port         SMALLINT UNSIGNED NOT NULL COMMENT '监听端口',
    name         VARCHAR(32) NOT NULL DEFAULT '' COMMENT '服务器名（便于日志/运维识别）',
    PRIMARY KEY (server_type, server_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

INSERT INTO ServerList (server_id, server_type, ip, port, name) VALUES
    (1, 0, '127.0.0.1', 9000, 'SuperServer'),
    (1, 1, '127.0.0.1', 9001, 'SessionServer'),
    (1, 2, '127.0.0.1', 9002, 'RecordServer'),
    (1, 3, '127.0.0.1', 9003, 'AOIServer'),
    (1, 4, '127.0.0.1', 9004, 'SceneServer'),
    (1, 5, '127.0.0.1', 9005, 'GatewayServer')
ON DUPLICATE KEY UPDATE ip=VALUES(ip), port=VALUES(port), name=VALUES(name);

DELETE FROM ServerList WHERE server_type IN (6, 7, 8);

-- ============================================================
-- Part C: rpg_global — GlobalServer 专用（全区杂项持久化）
-- ============================================================

CREATE DATABASE IF NOT EXISTS rpg_global DEFAULT CHARACTER SET utf8mb4;
USE rpg_global;

-- -----------------------------------------------------------
-- 表：AllLittleThing（全区杂项持久化 —— GlobalServer 读写）
-- 设计意图：跨游戏区共享的杂项数据（全区配置、排行榜快照、活动状态等）。
--           thing_key 全区唯一；thing_value 存序列化 blob。
-- -----------------------------------------------------------
CREATE TABLE IF NOT EXISTS AllLittleThing (
    id          BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY COMMENT '自增主键',
    thing_key   VARCHAR(64) NOT NULL COMMENT '业务键，全区唯一',
    thing_value MEDIUMBLOB COMMENT '序列化数据',
    update_time DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '最后更新时间',
    UNIQUE KEY uk_thing_key (thing_key)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- -------------------------------------------------------
-- 测试种子数据已拆分至 seed_test_data.sql（开发环境按需执行）
-- -------------------------------------------------------
