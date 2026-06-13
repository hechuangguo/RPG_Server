CREATE TABLE IF NOT EXISTS ServerList (
    server_id    INT UNSIGNED NOT NULL COMMENT '服务器实例编号（同类型下唯一，从 1 起）',
    server_type  TINYINT UNSIGNED NOT NULL COMMENT '服务器类型（区内：0 Super,1 Session,2 Record,3 AOI,4 Scene,5 Gateway）',
    ip           VARCHAR(64) NOT NULL DEFAULT '127.0.0.1' COMMENT '监听 IP',
    port         SMALLINT UNSIGNED NOT NULL COMMENT '监听端口',
    name         VARCHAR(32) NOT NULL DEFAULT '' COMMENT '服务器名（便于日志/运维识别）',
    PRIMARY KEY (server_type, server_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 游戏区默认拓扑（6 行）；幂等写入，可重复执行
INSERT INTO ServerList (server_id, server_type, ip, port, name) VALUES
    (1, 0, '127.0.0.1', 9000, 'SuperServer'),
    (1, 1, '127.0.0.1', 9001, 'SessionServer'),
    (1, 2, '127.0.0.1', 9002, 'RecordServer'),
    (1, 3, '127.0.0.1', 9003, 'AOIServer'),
    (1, 4, '127.0.0.1', 9004, 'SceneServer'),
    (1, 5, '127.0.0.1', 9005, 'GatewayServer')
ON DUPLICATE KEY UPDATE ip=VALUES(ip), port=VALUES(port), name=VALUES(name);