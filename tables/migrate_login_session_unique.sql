-- ============================================================
--  RPG — LoginSession 唯一约束（每账号每区仅一条有效票据）
--  库名：rpg_login
--  执行：mysql -h HOST -u USER -p rpg_login < tables/migrate_login_session_unique.sql
--  可重复执行：重复 uk_accid_zone 会报错可忽略
--
--  执行前清理重复行（保留 token 字典序较大者，通常为较新票据）
-- ============================================================

USE rpg_login;

DELETE t1 FROM LoginSession t1
INNER JOIN LoginSession t2
    ON t1.accid = t2.accid AND t1.zone_id = t2.zone_id AND t1.token < t2.token;

ALTER TABLE LoginSession
    ADD UNIQUE KEY uk_accid_zone (accid, zone_id);
