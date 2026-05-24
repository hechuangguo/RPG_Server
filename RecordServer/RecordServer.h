/**
 * @file    RecordServer.h
 * @brief  数据库服务器 —— 用户数据持久化（MySQL）、账号登录验证
 *
 * ## 职责
 * - MySQL 直连：账号验证、用户数据加载/保存
 * - 定时自动存档（每 60 秒 AutoSaveAll）
 * - 内部通信：连接 SuperServer + SessionServer
 *
 * ## 依赖关系
 * - 依赖 SuperServer（注册） + SessionServer（社会关系数据）
 * - 被 GatewayServer（登录验证）、SuperServer（加载用户）、SceneServer（保存用户）调用
 *
 * ## 数据流
 * @code
 *   GatewayServer ──(REC_LOGIN_VERIFY_REQ)──→ RecordServer ──(SQL)──→ MySQL
 *   SuperServer   ──(REC_LOAD_USER_REQ)───→ RecordServer ──(SQL)──→ MySQL
 *   SceneServer   ──(REC_SAVE_USER_REQ)───→ RecordServer ──(SQL)──→ MySQL
 * @endcode
 */

#pragma once
#include "../sdk/net/TcpServer.h"
#include "../sdk/net/TcpClient.h"
#include "../sdk/log/Logger.h"
#include "../sdk/timer/TimerMgr.h"
#include "../sdk/util/MsgDispatcher.h"
#include "../sdk/util/ConfigLoader.h"
#include "../sdk/util/UserWireUtil.h"
#include "../protocal/InternalMsg.h"
#include "RecordUser.h"
#include "RecordUserManager.h"
#include <string>
#include <vector>
#include <mysql/mysql.h>
#include <cinttypes>

/**
 * @brief RecordServer 核心类
 *
 * 单进程运行，持有 MySQL 连接，同时维护到 SuperServer 和 SessionServer 的 TcpClient。
 */
class RecordServer : public INetCallback
{
public:
    RecordServer()
        : m_server(this)
        , m_superClient(this)
        , m_sessionClient(this)
        , m_db(nullptr)
    {}
    ~RecordServer() { if (m_db) mysql_close(m_db); }

    /**
     * @brief 初始化 RecordServer
     * @param ip   监听 IP
     * @param port 监听端口
     * @param cfg  全局配置（含数据库连接参数）
     * @return 成功返回 true
     */
    bool Init(const std::string& ip, uint16_t port,
              const ServerConfig& cfg)
    {
        Logger::Instance().SetServerName("RecordServer");
        LOG_INFO("RecordServer starting on %s:%d", ip.c_str(), port);
        if (!m_server.Start(ip, port)) { LOG_FATAL("Start failed"); return false; }
        if (!InitDB(cfg))              { LOG_FATAL("DB init failed"); return false; }

        m_superClient.Connect(cfg.superIP, (uint16_t)cfg.superPort);
        m_sessionClient.Connect("127.0.0.1", (uint16_t)cfg.sessionPort);

        RegisterHandlers();

        TimerMgr::Instance().Register(500,   0,     [this]{ RegisterToSuper(); });
        TimerMgr::Instance().Register(10000, 10000, [this]{ SendHeartbeat(); });
        // 每 60 秒自动保存所有脏数据
        TimerMgr::Instance().Register(60000, 60000, [this]{ AutoSaveAll(); });
        LOG_INFO("RecordServer started.");
        return true;
    }

    /** @brief 主循环 */
    void Run()
    {
        while (true)
        {
            m_superClient.Poll(0);
            m_sessionClient.Poll(0);
            m_server.Poll(10);
            TimerMgr::Instance().Update();
        }
    }

    void OnConnect(ConnID id)    override { LOG_INFO("InnerConn=%u connected", id); }
    void OnDisconnect(ConnID id) override { LOG_WARN("InnerConn=%u disconnected", id); }
    void OnMessage(ConnID id, uint16_t msgID, const char* data, uint16_t len) override
    {
        MsgDispatcher::Instance().Dispatch(id, msgID, data, len);
    }

private:
    /**
     * @brief 初始化 MySQL 连接
     * @param cfg 数据库配置
     * @return 成功返回 true
     */
    bool InitDB(const ServerConfig& cfg)
    {
        m_db = mysql_init(nullptr);
        if (!m_db) return false;
        if (!mysql_real_connect(m_db,
            cfg.dbHost.c_str(), cfg.dbUser.c_str(),
            cfg.dbPass.c_str(), cfg.dbName.c_str(),
            (unsigned int)cfg.dbPort, nullptr, 0))
        {
            LOG_ERR("MySQL connect failed: %s", mysql_error(m_db));
            return false;
        }
        mysql_set_character_set(m_db, "utf8mb4");  /**< 设置 UTF-8 编码 */
        LOG_INFO("MySQL connected: %s:%d/%s", cfg.dbHost.c_str(), cfg.dbPort, cfg.dbName.c_str());
        return true;
    }

    /**
     * @brief 注册消息处理函数
     *
     * REC_LOGIN_VERIFY_REQ → OnLoginVerify（账号密码验证）
     * REC_LOAD_USER_REQ    → OnLoadUser（从 DB 加载用户）
     * REC_SAVE_USER_REQ    → OnSaveUser（保存用户到 DB）
     */
    void RegisterHandlers()
    {
        auto& d = MsgDispatcher::Instance();
        d.Register((uint16_t)InternalMsgID::REC_LOGIN_VERIFY_REQ,
            [this](uint32_t c, const char* d, uint16_t l){ OnLoginVerify(c, d, l); });
        d.Register((uint16_t)InternalMsgID::REC_LOAD_USER_REQ,
            [this](uint32_t c, const char* d, uint16_t l){ OnLoadUser(c, d, l); });
        d.Register((uint16_t)InternalMsgID::REC_SAVE_USER_REQ,
            [this](uint32_t c, const char* d, uint16_t l){ OnSaveUser(c, d, l); });
    }

    void RegisterToSuper()
    {
        Msg_S2S_Register reg{};
        reg.serverType = (uint8_t)SubServerType::RECORD;
        reg.serverID   = 1;
        strncpy(reg.ip, "127.0.0.1", sizeof(reg.ip));
        reg.port       = 9002;
        m_superClient.SendMsg((uint16_t)InternalMsgID::S2S_REGISTER_REQ,
                               reinterpret_cast<char*>(&reg), sizeof(reg));
    }

    void SendHeartbeat()
    {
        Msg_S2S_Heartbeat hb{};
        hb.seq = ++m_hbSeq;
        hb.timestamp = TimerMgr::NowMs();
        m_superClient.SendMsg((uint16_t)InternalMsgID::S2S_HEARTBEAT,
                               reinterpret_cast<char*>(&hb), sizeof(hb));
    }

    /**
     * @brief 账号密码验证
     *
     * 使用 SELECT ... WHERE account='?' AND password=MD5('?') 查询 t_account 表。
     *
     * @warning 【安全风险】当前实现使用 snprintf 拼接 SQL 字符串，存在 SQL 注入风险。
     *          如果 req->account 或 req->password 中包含单引号等特殊字符，
     *          攻击者可构造恶意输入绕过认证或执行任意 SQL。
     *          生产环境必须使用 mysql_real_escape_string() 转义用户输入，
     *          或改用 mysql_stmt_prepare() / mysql_stmt_bind_param() 参数化查询。
     */
    void OnLoginVerify(ConnID fromConn, const char* data, uint16_t len)
    {
        if (len < sizeof(Msg_REC_LoginVerifyReq)) return;
        const auto* req = reinterpret_cast<const Msg_REC_LoginVerifyReq*>(data);

        Msg_REC_LoginVerifyRsp rsp{};
        rsp.gatewayConnID = req->gatewayConnID;

        char sql[512];
        snprintf(sql, sizeof(sql),
                 "SELECT user_id FROM t_account WHERE account='%s' AND password=MD5('%s') LIMIT 1",
                 req->account, req->password);

        if (mysql_query(m_db, sql) != 0)
        {
            LOG_ERR("MySQL query failed: %s", mysql_error(m_db));
            rsp.code = -1;
        }
        else
        {
            MYSQL_RES* res = mysql_store_result(m_db);
            MYSQL_ROW  row = res ? mysql_fetch_row(res) : nullptr;
            if (row)
            {
                rsp.code    = 0;
                rsp.userID  = (uint64_t)strtoull(row[0], nullptr, 10);
            }
            else
            {
                rsp.code = 1;
            }
            if (res) mysql_free_result(res);
        }
        m_server.SendMsg(fromConn, (uint16_t)InternalMsgID::REC_LOGIN_VERIFY_RSP,
                         reinterpret_cast<char*>(&rsp), sizeof(rsp));
    }

    /**
     * @brief 加载用户数据
     *
     * 先从 m_users 缓存查找，未命中则调用 LoadUserFromDB 从 MySQL 加载。
     */
    void OnLoadUser(ConnID fromConn, const char* data, uint16_t len)
    {
        if (len < sizeof(UserID)) return;
        UserID rid = *reinterpret_cast<const UserID*>(data);

        if (!m_userManager.contains(rid))
            LoadUserFromDB(rid);

        Msg_REC_LoadUserRsp hdr{};
        hdr.userID = rid;
        auto user = m_userManager.findUser(rid);
        if (!user)
        {
            hdr.code = -1;
            m_server.SendMsg(fromConn, (uint16_t)InternalMsgID::REC_LOAD_USER_RSP,
                             reinterpret_cast<char*>(&hdr), sizeof(hdr));
            return;
        }

        hdr.code = 0;
        const auto& base = user->Base();
        UserBaseWire wire{};
        wire.userID   = base.userID;
        strncpy(wire.name, base.name.c_str(), sizeof(wire.name) - 1);
        wire.level    = base.level;
        wire.vocation = base.vocation;
        wire.sex      = base.sex;
        wire.mapID    = base.mapID ? base.mapID : 1001;
        wire.posX     = base.posX;
        wire.posY     = base.posY;
        wire.posZ     = base.posZ;
        wire.hp       = base.hp;
        wire.maxHP    = base.maxHP;
        wire.mp       = base.mp;
        wire.maxMP    = base.maxMP;
        wire.gold     = base.gold;

        std::vector<char> buf(sizeof(hdr) + sizeof(wire));
        memcpy(buf.data(), &hdr, sizeof(hdr));
        memcpy(buf.data() + sizeof(hdr), &wire, sizeof(wire));
        m_server.SendMsg(fromConn, (uint16_t)InternalMsgID::REC_LOAD_USER_RSP,
                         buf.data(), (uint16_t)buf.size());
    }

    /** @brief 处理用户保存请求 */
    void OnSaveUser(ConnID fromConn, const char* data, uint16_t len)
    {
        UserID rid = INVALID_USER_ID;

        if (len >= sizeof(Msg_REC_SaveUserReq))
        {
            const auto* req = reinterpret_cast<const Msg_REC_SaveUserReq*>(data);
            rid = req->userID;

            auto user = m_userManager.findUser(rid);
            if (!user)
            {
                UserBase base;
                applyUserBaseWire(base, req->wire);
                user = RecordUser::create(base);
                user->init();
                m_userManager.addUser(rid, user);
            }
            else
            {
                applyUserBaseWire(user->Base(), req->wire);
                user->markDirty();
            }
        }
        else if (len >= sizeof(UserID))
        {
            rid = *reinterpret_cast<const UserID*>(data);
        }
        else
        {
            return;
        }

        SaveUserToDB(rid);
        Msg_REC_LoadUserRsp rsp{};
        rsp.code   = 0;
        rsp.userID = rid;
        m_server.SendMsg(fromConn, (uint16_t)InternalMsgID::REC_SAVE_USER_RSP,
                         reinterpret_cast<char*>(&rsp), sizeof(rsp));
    }

    /**
     * @brief 从 MySQL 加载用户数据到内存
     *
     * 查询 t_charbase 表，构建 UserBase 并创建 RecordUser 对象。
     *
     * @note SELECT 字段与 row 索引映射表：
     *       | 索引 | 字段      | 类型      | 说明               |
     *       |------|-----------|-----------|--------------------|
     *       | row[0]  | user_id  | uint64_t  | 用户唯一标识       |
     *       | row[1]  | name     | string    | 用户昵称           |
     *       | row[2]  | level    | uint32_t  | 等级（默认 1）     |
     *       | row[3]  | vocation | uint32_t  | 职业（默认 0）     |
     *       | row[4]  | sex      | uint32_t  | 性别（默认 0）     |
     *       | row[5]  | map_id   | uint32_t  | 所在地图（默认 0） |
     *       | row[6]  | pos_x    | float     | X 坐标            |
     *       | row[7]  | pos_y    | float     | Y 坐标            |
     *       | row[8]  | pos_z    | float     | Z 坐标            |
     *       | row[9]  | hp       | uint32_t  | 当前生命值（默认 100）|
     *       | row[10] | mp       | uint32_t  | 当前魔法值（默认 100）|
     *       | row[11] | gold     | uint64_t  | 金币              |
     */
    void LoadUserFromDB(UserID rid)
    {
        char sql[256];
        snprintf(sql, sizeof(sql),
                 "SELECT user_id,name,level,vocation,sex,map_id,pos_x,pos_y,pos_z,"
                 "hp,max_hp,mp,max_mp,gold"
                 " FROM t_charbase WHERE user_id=%" PRIu64 " LIMIT 1", rid);
        if (mysql_query(m_db, sql) != 0) { LOG_ERR("LoadUser SQL err: %s", mysql_error(m_db)); return; }
        MYSQL_RES* res = mysql_store_result(m_db);
        MYSQL_ROW  row = res ? mysql_fetch_row(res) : nullptr;
        if (row)
        {
            UserBase base;
            base.userID  = rid;
            base.name    = row[1] ? row[1] : "";
            base.level   = row[2] ? (uint32_t)atoi(row[2]) : 1;
            base.vocation= row[3] ? (uint32_t)atoi(row[3]) : 0;
            base.sex     = row[4] ? (uint32_t)atoi(row[4]) : 0;
            base.mapID   = row[5] ? (uint32_t)atoi(row[5]) : 0;
            base.posX    = row[6] ? (float)atof(row[6]) : 0.f;
            base.posY    = row[7] ? (float)atof(row[7]) : 0.f;
            base.posZ    = row[8] ? (float)atof(row[8]) : 0.f;
            base.hp      = row[9] ? (uint32_t)atoi(row[9])  : 100;
            base.maxHP   = row[10]? (uint32_t)atoi(row[10]) : 100;
            base.mp      = row[11]? (uint32_t)atoi(row[11]) : 100;
            base.maxMP   = row[12]? (uint32_t)atoi(row[12]) : 100;
            base.gold    = row[13]? (uint64_t)strtoull(row[13], nullptr, 10) : 0;
            auto user = RecordUser::create(base);
            user->init();
            user->load();
            m_userManager.addUser(rid, user);
            LOG_DEBUG("LoadUserFromDB: userID=%llu name=%s", rid, base.name.c_str());
        }
        if (res) mysql_free_result(res);
    }

    /**
     * @brief 将用户数据写回 MySQL
     *
     * UPDATE t_charbase SET ... WHERE user_id=...
     */
    void SaveUserToDB(UserID rid)
    {
        auto user = m_userManager.findUser(rid);
        if (!user) return;
        user->save();
        const auto& base = user->Base();
        char sql[768];
        snprintf(sql, sizeof(sql),
                 "INSERT INTO t_charbase (user_id,name,level,vocation,sex,map_id,"
                 "pos_x,pos_y,pos_z,hp,max_hp,mp,max_mp,gold)"
                 " VALUES (%" PRIu64 ",'%s',%u,%u,%u,%u,%.2f,%.2f,%.2f,%u,%u,%u,%u,%" PRIu64 ")"
                 " ON DUPLICATE KEY UPDATE name=VALUES(name),level=VALUES(level),"
                 " vocation=VALUES(vocation),sex=VALUES(sex),map_id=VALUES(map_id),"
                 " pos_x=VALUES(pos_x),pos_y=VALUES(pos_y),pos_z=VALUES(pos_z),"
                 " hp=VALUES(hp),max_hp=VALUES(max_hp),mp=VALUES(mp),"
                 " max_mp=VALUES(max_mp),gold=VALUES(gold)",
                 rid, base.name.c_str(), base.level, base.vocation, base.sex, base.mapID,
                 base.posX, base.posY, base.posZ,
                 base.hp, base.maxHP, base.mp, base.maxMP, base.gold);
        if (mysql_query(m_db, sql) != 0)
            LOG_ERR("SaveUser SQL err: %s", mysql_error(m_db));
        else
            LOG_DEBUG("SaveUserToDB: userID=%llu", rid);
    }

    /**
     * @brief 批量自动存档
     *
     * 每 60 秒定时触发，遍历所有 m_users 调用 SaveUserToDB。
     *
     * @note 脏标记检查逻辑：当前实现无条件保存全部用户数据。
     *       若需优化 I/O 开销，可增加脏标记（dirty flag）过滤：
     *       仅当用户数据被修改（dirty == true）时才执行 SaveUserToDB，
     *       保存成功后将 dirty 重置为 false。
     */
    void AutoSaveAll()
    {
        m_userManager.forEach([this](UserID rid, RecordUser& /*user*/)
        {
            SaveUserToDB(rid);
        });
        LOG_INFO("AutoSave: %zu users saved.", m_userManager.getUserCount());
    }

    TcpServer  m_server;          /**< 内部连接监听（接收来自 Gateway/Super/Scene Server 的请求） */
    TcpClient  m_superClient;     /**< 到 SuperServer 的连接（用于注册服务器、发送心跳） */
    TcpClient  m_sessionClient;   /**< 到 SessionServer 的连接（用于查询社会关系/好友数据） */
    MYSQL*     m_db;              /**< MySQL 连接句柄（全局共享，非线程安全） */
    uint32_t   m_hbSeq = 0;       /**< 心跳序列号（每次自增，SuperServer 用于检测丢包） */

    RecordUserManager m_userManager;
};
