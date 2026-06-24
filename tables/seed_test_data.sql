-- ============================================================
--  RPG 游戏服务器测试种子数据脚本（仅开发/测试环境）
--  数据库：rpg_game + rpg_login（须先执行 tables/init.sql）
--  说明：
--    1) CharBase 须含 accid/gamezone（与 GameUser 对齐）
--    2) binary 字段示例写入空二进制 x''
--    3) INSERT IGNORE 幂等，可重复执行；生产环境禁止导入
-- ============================================================

USE rpg_login;

-- 测试账号（与 CharBase accid=1 对齐；wire 传 SHA-256 摘要，存库 bcrypt(hex(digest))）
-- 明文 123456 → sha256 8d969eef... → 见 ./scripts/gen_password_digest.sh
INSERT IGNORE INTO GameUser (accid, account, password_hash, user_id, gamezone)
VALUES
    (1, 'test001', '$2b$12$XNwxnI6R/brmoCk7q2Qsc.kkGKjS8mczYvU39YJIIRR5LzWnQRnJ6', 1, 1),
    (2, 'autotest_e2e', '$2b$12$68V4uzaOgOdTLEG8KpKI8unX9IlQ.tkzz10LNSqdshJcQIp3x/E5S', 0, 1);

USE rpg_game;

-- -----------------------------------------------------------
-- 测试角色基础数据（CharBase）
-- 用途：Gateway 角色列表 / 选角进世界联调；map_id=1001 对应新手村
-- -----------------------------------------------------------
INSERT IGNORE INTO CharBase
    (user_id, accid, gamezone, name, level, vocation, sex, model_id, map_id, pos_x, pos_y, pos_z, hp, max_hp, mp, max_mp, gold, `binary`)
VALUES
    (1, 1, 1, 'test001', 1, 1, 1, 1, 1001, 100, 0, 100, 100, 100, 100, 100, 1000, x''),
    (2, 1, 1, 'test002', 5, 2, 1, 2, 1002, 10, 0, 8, 180, 200, 120, 150, 5000, x''),
    (3, 1, 1, 'test003', 10, 3, 1, 3, 2001, 22, 0, 16, 260, 300, 180, 220, 12000, x'');

-- 修正历史种子 sex=2（旧值超出 MAX_SEX_ID=1：0=男 1=女）
UPDATE CharBase SET sex = 1 WHERE user_id = 3 AND sex = 2;

-- -------------------------------------------------------
-- 测试社交占位（SessionServer 读 Relation）
-- -------------------------------------------------------
INSERT IGNORE INTO Relation (user_id, friends_json, blacklist_json, guild_id, team_id, `binary`)
VALUES
    (1, '2,3', '', 0, 0, x''),
    (2, '1',   '', 0, 0, x''),
    (3, '1',   '', 0, 0, x'');
