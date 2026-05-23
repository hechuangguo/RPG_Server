/**
 * @file    RecordServer.h
 * @brief  数据库服务器 —— 角色数据持久化（MySQL）、账号登录验证
 *
 * ## 职责
 * - MySQL 直连：账号验证、角色数据加载/保存
 * - 定时自动存档（每 60 秒 AutoSaveAll）
 * - 内部通信：连接 SuperServer + SessionServer
 *
 * ## 依赖关系
 * - 依赖 SuperServer（注册） + SessionServer（社会关系数据）
 * - 被 GatewayServer（登录验证）、SuperServer（加载角色）、SceneServer（保存角色）调用
 *
 * ## 数据流
 * @code
 *   GatewayServer ──(REC_LOGIN_VERIFY_REQ)──→ RecordServer ──(SQL)──→ MySQL
 *   SuperServer   ──(REC_LOAD_ROLE_REQ)───→ RecordServer ──(SQL)──→ MySQL
 *   SceneServer   ──(REC_SAVE_ROLE_REQ)───→ RecordServer ──(SQL)──→ MySQL
 * @endcode
 */

#pragma once
#include "../sdk/net/TcpServer.h"
#include "../sdk/net/TcpClient.h"
#include "../sdk/util/RoleBase.h"
#include "../sdk/log/Logger.h"
#include "../sdk/timer/TimerMgr.h"
#include "../sdk/util/MsgDispatcher.h"
#include "../protocal/InternalMsg.h"
#include <unordered_map>
#include <string>
#include <mysql/mysql.h>   /**< libmysqlclient C API */

/**
 * @brief RecordServer 中的角色数据（完整存档）
 *
 * 包含 RoleBase 基础属性以及背包、技能、任务等 JSON 串。
 */
struct RoleRecord
{
    RoleBase       base;           /**< 基础角色数据 */
    std::string    bagJson;        /**< 背包数据（JSON 序列化） */
    std::string    skillJson;      /**< 技能数据（JSON 序列化） */
    std::string    questJson;      /**< 任务数据（JSON 序列化） */
    uint64_t       lastSaveTime = 0; /**< 上次存档时间（ms） */
    bool           dirty        = false; /**< 脏标记（数据变更后置 true） */
};

/**
 * @brief RecordServer 中的角色对象
 *
 * 继承 IRole，额外持有 RoleRecord（完整持久化数据）。
 */
class RecordRole : public IRole
{
public:
    explicit RecordRole(const RoleBase& base)
        : IRole(base) {}
    RoleRecord& Record() { return m_record; }
private:
    RoleRecord m_record;
};

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
     * REC_LOAD_ROLE_REQ    → OnLoadRole（从 DB 加载角色）
     * REC_SAVE_ROLE_REQ    → OnSaveRole（保存角色到 DB）
     */
    void RegisterHandlers()
    {
        auto& d = MsgDispatcher::Instance();
        d.Register((uint16_t)InternalMsgID::REC_LOGIN_VERIFY_REQ,
            [this](uint32_t c, const char* d, uint16_t l){ OnLoginVerify(c, d, l); });
        d.Register((uint16_t)InternalMsgID::REC_LOAD_ROLE_REQ,
            [this](uint32_t c, const char* d, uint16_t l){ OnLoadRole(c, d, l); });
        d.Register((uint16_t)InternalMsgID::REC_SAVE_ROLE_REQ,
            [this](uint32_t c, const char* d, uint16_t l){ OnSaveRole(c, d, l); });
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
     * @note 生产环境应使用参数化查询防止 SQL 注入。
     */
    void OnLoginVerify(ConnID fromConn, const char* data, uint16_t len)
    {
        if (len < sizeof(Msg_REC_LoginVerifyReq)) return;
        const auto* req = reinterpret_cast<const Msg_REC_LoginVerifyReq*>(data);

        Msg_REC_LoginVerifyRsp rsp{};
        rsp.gatewayConnID = req->gatewayConnID;

        char sql[512];
        snprintf(sql, sizeof(sql),
                 "SELECT role_id FROM t_account WHERE account='%s' AND password=MD5('%s') LIMIT 1",
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
                rsp.code   = 0;
                rsp.roleID = (uint64_t)strtoull(row[0], nullptr, 10);
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
     * @brief 加载角色数据
     *
     * 先从 m_roles 缓存查找，未命中则调用 LoadRoleFromDB 从 MySQL 加载。
     */
    void OnLoadRole(ConnID fromConn, const char* data, uint16_t len)
    {
        if (len < sizeof(RoleID)) return;
        RoleID rid = *reinterpret_cast<const RoleID*>(data);

        auto it = m_roles.find(rid);
        if (it == m_roles.end())
        {
            LoadRoleFromDB(rid);  /**< 从 DB 加载 */
        }
        Msg_REC_LoadRoleRsp rsp{};
        rsp.code   = m_roles.count(rid) ? 0 : -1;
        rsp.roleID = rid;
        m_server.SendMsg(fromConn, (uint16_t)InternalMsgID::REC_LOAD_ROLE_RSP,
                         reinterpret_cast<char*>(&rsp), sizeof(rsp));
    }

    /** @brief 处理角色保存请求 */
    void OnSaveRole(ConnID fromConn, const char* data, uint16_t len)
    {
        if (len < sizeof(RoleID)) return;
        RoleID rid = *reinterpret_cast<const RoleID*>(data);
        SaveRoleToDB(rid);
        Msg_REC_LoadRoleRsp rsp{};
        rsp.code   = 0;
        rsp.roleID = rid;
        m_server.SendMsg(fromConn, (uint16_t)InternalMsgID::REC_SAVE_ROLE_RSP,
                         reinterpret_cast<char*>(&rsp), sizeof(rsp));
    }

    /**
     * @brief 从 MySQL 加载角色数据到内存
     *
     * 查询 t_role 表所有字段，构建 RoleBase 并创建 RecordRole 对象。
     */
    void LoadRoleFromDB(RoleID rid)
    {
        char sql[256];
        snprintf(sql, sizeof(sql),
                 "SELECT role_id,name,level,vocation,sex,map_id,pos_x,pos_y,pos_z,hp,mp,gold"
                 " FROM t_role WHERE role_id=%llu LIMIT 1", rid);
        if (mysql_query(m_db, sql) != 0) { LOG_ERR("LoadRole SQL err: %s", mysql_error(m_db)); return; }
        MYSQL_RES* res = mysql_store_result(m_db);
        MYSQL_ROW  row = res ? mysql_fetch_row(res) : nullptr;
        if (row)
        {
            RoleBase base;
            base.roleID  = rid;
            base.name    = row[1] ? row[1] : "";
            base.level   = row[2] ? (uint32_t)atoi(row[2]) : 1;
            base.vocation= row[3] ? (uint32_t)atoi(row[3]) : 0;
            base.sex     = row[4] ? (uint32_t)atoi(row[4]) : 0;
            base.mapID   = row[5] ? (uint32_t)atoi(row[5]) : 0;
            base.posX    = row[6] ? (float)atof(row[6]) : 0.f;
            base.posY    = row[7] ? (float)atof(row[7]) : 0.f;
            base.posZ    = row[8] ? (float)atof(row[8]) : 0.f;
            base.hp      = row[9] ? (uint32_t)atoi(row[9])  : 100;
            base.mp      = row[10]? (uint32_t)atoi(row[10]) : 100;
            base.gold    = row[11]? (uint64_t)strtoull(row[11], nullptr, 10) : 0;
            auto role    = std::make_shared<RecordRole>(base);
            m_roles[rid] = role;
            LOG_DEBUG("LoadRoleFromDB: roleID=%llu name=%s", rid, base.name.c_str());
        }
        if (res) mysql_free_result(res);
    }

    /**
     * @brief 将角色数据写回 MySQL
     *
     * UPDATE t_role SET level=..., map_id=..., ..., WHERE role_id=...
     */
    void SaveRoleToDB(RoleID rid)
    {
        auto it = m_roles.find(rid);
        if (it == m_roles.end()) return;
        const auto& base = it->second->Base();
        char sql[512];
        snprintf(sql, sizeof(sql),
                 "UPDATE t_role SET level=%u,map_id=%u,pos_x=%.2f,pos_y=%.2f,pos_z=%.2f,"
                 "hp=%u,mp=%u,gold=%llu WHERE role_id=%llu",
                 base.level, base.mapID, base.posX, base.posY, base.posZ,
                 base.hp, base.mp, base.gold, rid);
        if (mysql_query(m_db, sql) != 0)
            LOG_ERR("SaveRole SQL err: %s", mysql_error(m_db));
        else
            LOG_DEBUG("SaveRoleToDB: roleID=%llu", rid);
    }

    /**
     * @brief 批量自动存档
     *
     * 每 60 秒定时触发，遍历所有 m_roles 调用 SaveRoleToDB。
     */
    void AutoSaveAll()
    {
        for (auto& [rid, role] : m_roles)
            SaveRoleToDB(rid);
        LOG_INFO("AutoSave: %zu roles saved.", m_roles.size());
    }

    TcpServer  m_server;          /**< 内部连接监听 */
    TcpClient  m_superClient;     /**< 到 SuperServer 的连接 */
    TcpClient  m_sessionClient;   /**< 到 SessionServer 的连接 */
    MYSQL*     m_db;              /**< MySQL 连接句柄 */
    uint32_t   m_hbSeq = 0;       /**< 心跳序列号 */

    /** @brief 角色数据缓存：roleID → RecordRole */
    std::unordered_map<RoleID, std::shared_ptr<RecordRole>> m_roles;
};
