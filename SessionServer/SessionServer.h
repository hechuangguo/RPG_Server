/**
 * @file    SessionServer.h
 * @brief  会话服务器 —— 社会关系直连 MySQL（t_relation）
 */

#pragma once
#include "../sdk/net/TcpServer.h"
#include "../sdk/net/TcpClient.h"
#include "../sdk/util/MsgDispatcher.h"
#include "../sdk/util/ConfigLoader.h"
#include "../sdk/log/Logger.h"
#include "../sdk/timer/TimerMgr.h"
#include "../protocal/InternalMsg.h"
#include "SessionUser.h"
#include "SessionUserManager.h"
#include <mysql/mysql.h>
#include <string>

class SessionServer : public INetCallback
{
public:
    SessionServer() : m_server(this), m_superClient(this), m_db(nullptr) {}
    ~SessionServer() { if (m_db) mysql_close(m_db); }

    bool Init(const std::string& ip, uint16_t port,
              const ServerConfig& cfg)
    {
        Logger::Instance().SetServerName("SessionServer");
        LOG_INFO("SessionServer starting on %s:%d", ip.c_str(), port);
        if (!m_server.Start(ip, port)) { LOG_FATAL("Start failed"); return false; }
        if (!InitDB(cfg)) { LOG_FATAL("DB init failed"); return false; }

        if (!m_superClient.Connect(cfg.superIP, (uint16_t)cfg.superPort))
            LOG_WARN("Cannot connect to SuperServer");

        RegisterHandlers();

        TimerMgr::Instance().Register(500, 0, [this]{ RegisterToSuper(); });
        TimerMgr::Instance().Register(10000, 10000, [this]{ SendHeartbeat(); });
        TimerMgr::Instance().Register(60000, 60000, [this]{ AutoSaveAll(); });
        LOG_INFO("SessionServer started (MySQL t_relation).");
        return true;
    }

    void Run()
    {
        while (true)
        {
            m_superClient.Poll(0);
            m_server.Poll(10);
            TimerMgr::Instance().Update();
        }
    }

    void OnConnect(ConnID id) override { LOG_INFO("InnerConn connected=%u", id); }
    void OnDisconnect(ConnID id) override { LOG_INFO("InnerConn disconnected=%u", id); }
    void OnMessage(ConnID id, uint16_t msgID, const char* data, uint16_t len) override
    {
        MsgDispatcher::Instance().Dispatch(id, msgID, data, len);
    }

private:
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
        mysql_set_character_set(m_db, "utf8mb4");
        LOG_INFO("SessionServer MySQL connected: %s:%d/%s",
                 cfg.dbHost.c_str(), cfg.dbPort, cfg.dbName.c_str());
        return true;
    }

    void RegisterHandlers()
    {
        auto& d = MsgDispatcher::Instance();
        d.Register((uint16_t)InternalMsgID::S2S_HEARTBEAT_ACK,
            [](uint32_t, const char*, uint16_t){ });
        d.Register((uint16_t)InternalMsgID::SES_LOAD_USER_REQ,
            [this](uint32_t c, const char* d, uint16_t l){ OnLoadUserReq(c, d, l); });
        d.Register((uint16_t)InternalMsgID::SES_SAVE_USER_REQ,
            [this](uint32_t c, const char* d, uint16_t l){ OnSaveUserReq(c, d, l); });
        d.Register((uint16_t)InternalMsgID::SES_FRIEND_UPDATE,
            [this](uint32_t c, const char* d, uint16_t l){ OnFriendUpdate(c, d, l); });
    }

    void RegisterToSuper()
    {
        Msg_S2S_Register reg{};
        reg.serverType = (uint8_t)SubServerType::SESSION;
        reg.serverID   = 1;
        strncpy(reg.ip, "127.0.0.1", sizeof(reg.ip));
        reg.port       = 9001;
        m_superClient.SendMsg((uint16_t)InternalMsgID::S2S_REGISTER_REQ,
                               reinterpret_cast<char*>(&reg), sizeof(reg));
    }

    void SendHeartbeat()
    {
        Msg_S2S_Heartbeat hb{};
        hb.seq       = ++m_hbSeq;
        hb.timestamp = TimerMgr::NowMs();
        m_superClient.SendMsg((uint16_t)InternalMsgID::S2S_HEARTBEAT,
                               reinterpret_cast<char*>(&hb), sizeof(hb));
    }

    /** @brief 从 t_relation 加载社会关系并上线 */
    void OnLoadUserReq(ConnID fromConn, const char* data, uint16_t len)
    {
        if (len < sizeof(UserID)) return;
        UserID uid = *reinterpret_cast<const UserID*>(data);
        LOG_DEBUG("LoadUser req userID=%llu", uid);

        auto user = m_userManager.getOrCreateUser(uid);
        user->load(m_db);
        user->onOnline();

        m_server.SendMsg(fromConn, (uint16_t)InternalMsgID::SES_LOAD_USER_RSP,
                         data, len);
    }

    /** @brief 保存社会关系到 t_relation */
    void OnSaveUserReq(ConnID /*fromConn*/, const char* data, uint16_t len)
    {
        if (len < sizeof(UserID)) return;
        UserID uid = *reinterpret_cast<const UserID*>(data);
        auto user = m_userManager.findUser(uid);
        if (!user)
            user = m_userManager.getOrCreateUser(uid);
        user->save(m_db);
    }

    void OnFriendUpdate(ConnID /*fromConn*/, const char* /*data*/, uint16_t len)
    {
        LOG_DEBUG("FriendUpdate len=%d", len);
    }

    void AutoSaveAll()
    {
        m_userManager.forEach([this](UserID uid, const std::shared_ptr<SessionUser>& user)
        {
            (void)uid;
            if (user->needSave())
                user->save(m_db);
        });
    }

    void PushOfflineMsg(UserID toID, uint16_t msgID,
                        const char* data, uint16_t len)
    {
        m_userManager.pushOfflineMsg(toID, msgID, data, len);
    }

    TcpServer          m_server;
    TcpClient          m_superClient;
    MYSQL*             m_db;
    uint32_t           m_hbSeq = 0;
    SessionUserManager m_userManager;
};
