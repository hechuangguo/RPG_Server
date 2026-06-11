/**
 * @file    LoginServer.cpp
 * @brief   LoginServer 实现
 */

#include "LoginServer.h"
#include "../sdk/timer/TimerMgr.h"

#include <cstdio>
#include <cstring>

struct LoginServer::ClientPortBridge : INetCallback
{
    explicit ClientPortBridge(LoginServer* owner)
        : m_owner(owner)
    {}

    void OnConnect(ConnID id) override { m_owner->onClientConnect(id); }

    void OnDisconnect(ConnID id) override { m_owner->onClientDisconnect(id); }

    void OnMessage(ConnID id, uint8_t module, uint8_t sub,
                   const char* data, uint16_t len) override
    {
        m_owner->onClientMessage(id, module, sub, data, len);
    }

    LoginServer* m_owner;
};

struct LoginServer::RegisterPortBridge : INetCallback
{
    explicit RegisterPortBridge(LoginServer* owner)
        : m_owner(owner)
    {}

    void OnConnect(ConnID id) override { m_owner->onRegisterConnect(id); }

    void OnDisconnect(ConnID id) override { m_owner->onRegisterDisconnect(id); }

    void OnMessage(ConnID id, uint8_t module, uint8_t sub,
                   const char* data, uint16_t len) override
    {
        m_owner->onRegisterMessage(id, module, sub, data, len);
    }

    LoginServer* m_owner;
};

LoginServer::LoginServer()
    : m_clientBridge(std::make_unique<ClientPortBridge>(this))
    , m_registerBridge(std::make_unique<RegisterPortBridge>(this))
    , m_clientServer(m_clientBridge.get())
    , m_registerServer(m_registerBridge.get())
{
}

LoginServer::~LoginServer() = default;

bool LoginServer::initDatabase(const DatabaseConfig& dbCfg)
{
    if (!dbCfg.configured)
        return true;
    m_dbRequired = true;
    m_db = mysql_init(nullptr);
    if (!m_db)
    {
        LOG_FATAL("LoginServer mysql_init failed");
        return false;
    }
    if (!mysql_real_connect(m_db, dbCfg.host.c_str(), dbCfg.user.c_str(), dbCfg.pass.c_str(),
                            dbCfg.name.c_str(), static_cast<unsigned int>(dbCfg.port),
                            nullptr, 0))
    {
        LOG_FATAL("LoginServer MySQL connect failed: %s", mysql_error(m_db));
        mysql_close(m_db);
        m_db = nullptr;
        return false;
    }
    mysql_set_character_set(m_db, "utf8mb4");
    LOG_INFO("LoginServer MySQL connected: %s:%d/%s",
             dbCfg.host.c_str(), dbCfg.port, dbCfg.name.c_str());
    return true;
}

bool LoginServer::Init(const LoginExternConfig& cfg)
{
    Logger::Instance().SetServerName("LoginServer");
    if (cfg.clientListenPort == 0 || cfg.registerListenPort == 0)
    {
        LOG_FATAL("LoginServer: ClientListen/RegisterListen port required");
        return false;
    }
    if (!initDatabase(cfg.database))
        return false;

    if (!m_clientServer.Start(cfg.clientListenIP, cfg.clientListenPort))
    {
        LOG_FATAL("LoginServer client listen failed on %s:%u",
                  cfg.clientListenIP.c_str(), cfg.clientListenPort);
        return false;
    }
    if (!m_registerServer.Start(cfg.registerListenIP, cfg.registerListenPort))
    {
        LOG_FATAL("LoginServer register listen failed on %s:%u",
                  cfg.registerListenIP.c_str(), cfg.registerListenPort);
        return false;
    }

    registerHandlers();
    TimerMgr::Instance().Register(10000, 10000, [this] { pruneGatewayTable(); });
    LOG_INFO("LoginServer started: client=%s:%u register=%s:%u",
             cfg.clientListenIP.c_str(), cfg.clientListenPort,
             cfg.registerListenIP.c_str(), cfg.registerListenPort);
    return true;
}

void LoginServer::Run()
{
    while (true)
    {
        m_clientServer.Poll(5);
        m_registerServer.Poll(5);
        TimerMgr::Instance().Update();
    }
}

void LoginServer::onClientConnect(ConnID id)
{
    LOG_INFO("Login client connected: conn=%u", id);
}

void LoginServer::onClientDisconnect(ConnID id)
{
    LOG_INFO("Login client disconnected: conn=%u", id);
}

void LoginServer::onClientMessage(ConnID id, uint8_t module, uint8_t sub,
                                   const char* data, uint16_t len)
{
    if (module == static_cast<uint8_t>(ClientModule::LOGIN) && sub == 0x01)
        onClientLogin(id, data, len);
}

void LoginServer::onRegisterConnect(ConnID id)
{
    LOG_INFO("Gateway register connected: conn=%u", id);
}

void LoginServer::onRegisterDisconnect(ConnID id)
{
    LOG_INFO("Gateway register disconnected: conn=%u", id);
}

void LoginServer::onRegisterMessage(ConnID id, uint8_t module, uint8_t sub,
                                     const char* data, uint16_t len)
{
    MsgDispatcher::Instance().Dispatch(id, module, sub, data, len);
}

void LoginServer::registerHandlers()
{
    auto& d = MsgDispatcher::Instance();
    d.Register((uint16_t)InternalMsgID::LOGIN_GATEWAY_REGISTER_REQ,
               [this](uint32_t c, const char* d, uint16_t l) { onGatewayRegister(c, d, l); });
    d.Register((uint16_t)InternalMsgID::LOGIN_GATEWAY_HEARTBEAT,
               [this](uint32_t c, const char* d, uint16_t l) { onGatewayHeartbeat(c, d, l); });
}

void LoginServer::onGatewayRegister(ConnID fromConn, const char* data, uint16_t len)
{
    if (len < sizeof(Msg_Login_GatewayRegister))
        return;
    const auto* req = reinterpret_cast<const Msg_Login_GatewayRegister*>(data);

    LoginGatewayEntry entry;
    entry.gatewayServerId = req->gatewayServerId;
    entry.ip = req->ip;
    entry.port = req->port;
    entry.name = req->name;
    entry.zoneName = req->zoneName;
    entry.lastHeartbeatMs = TimerMgr::NowMs();
    m_gatewayRegistry.upsert(entry);

    Msg_Login_GatewayRegisterRsp rsp{};
    rsp.code = 0;
    rsp.gatewayServerId = req->gatewayServerId;
    m_registerServer.SendMsg(fromConn,
                             (uint16_t)InternalMsgID::LOGIN_GATEWAY_REGISTER_RSP,
                             reinterpret_cast<char*>(&rsp), sizeof(rsp));
    LOG_INFO("Gateway registered: id=%u %s:%u name=%s (total=%zu)",
             req->gatewayServerId, req->ip, req->port, req->name, m_gatewayRegistry.size());
}

void LoginServer::onGatewayHeartbeat(ConnID fromConn, const char* data, uint16_t len)
{
    if (len < sizeof(Msg_Login_GatewayRegister))
        return;
    const auto* req = reinterpret_cast<const Msg_Login_GatewayRegister*>(data);
    if (!m_gatewayRegistry.touch(req->gatewayServerId))
    {
        onGatewayRegister(fromConn, data, len);
        return;
    }
    LOG_DEBUG("Gateway heartbeat: id=%u", req->gatewayServerId);
}

void LoginServer::onClientLogin(ConnID connID, const char* data, uint16_t len)
{
    if (len < sizeof(Msg_C2S_LoginReq))
        return;
    const auto* req = reinterpret_cast<const Msg_C2S_LoginReq*>(data);

    Msg_S2C_LoginRsp loginRsp{};
    loginRsp.userID = 0;

    if (m_dbRequired && !m_db)
    {
        loginRsp.code = -1;
        copyToWire(loginRsp.msg, sizeof(loginRsp.msg), "Login service unavailable");
        m_clientServer.SendMsg(connID, (uint16_t)ClientMsgID::S2C_LOGIN_RSP,
                               reinterpret_cast<char*>(&loginRsp), sizeof(loginRsp));
        sendGatewayInfo(connID, -1, "No gateway");
        return;
    }

    char accName[sizeof(req->account)];
    copyToWire(accName, sizeof(accName), req->account);

    if (m_db)
    {
        char escName[sizeof(accName) * 2 + 1];
        mysql_real_escape_string(m_db, escName, accName, strlen(accName));
        char sql[256];
        snprintf(sql, sizeof(sql),
                 "SELECT user_id FROM CharBase WHERE name='%s' LIMIT 1", escName);

        if (mysql_query(m_db, sql) != 0)
        {
            LOG_ERR("LoginServer MySQL query failed: %s", mysql_error(m_db));
            loginRsp.code = -1;
            copyToWire(loginRsp.msg, sizeof(loginRsp.msg), "Database error");
        }
        else
        {
            MYSQL_RES* res = mysql_store_result(m_db);
            MYSQL_ROW row = res ? mysql_fetch_row(res) : nullptr;
            if (row)
            {
                loginRsp.code = 0;
                loginRsp.userID = static_cast<uint64_t>(strtoull(row[0], nullptr, 10));
                copyToWire(loginRsp.msg, sizeof(loginRsp.msg), "Login OK");
            }
            else
            {
                snprintf(sql, sizeof(sql),
                         "INSERT INTO CharBase (name) VALUES ('%s')", escName);
                if (mysql_query(m_db, sql) != 0)
                {
                    LOG_ERR("LoginServer auto-create failed: %s", mysql_error(m_db));
                    loginRsp.code = -1;
                    copyToWire(loginRsp.msg, sizeof(loginRsp.msg), "Create account failed");
                }
                else
                {
                    loginRsp.code = 0;
                    loginRsp.userID = static_cast<uint64_t>(mysql_insert_id(m_db));
                    copyToWire(loginRsp.msg, sizeof(loginRsp.msg), "Login OK");
                    LOG_INFO("LoginServer auto-create: name=%s userID=%llu",
                             accName, static_cast<unsigned long long>(loginRsp.userID));
                }
            }
            if (res)
                mysql_free_result(res);
        }
    }
    else
    {
        loginRsp.code = 0;
        copyToWire(loginRsp.msg, sizeof(loginRsp.msg), "Login OK (no DB)");
    }

    m_clientServer.SendMsg(connID, (uint16_t)ClientMsgID::S2C_LOGIN_RSP,
                           reinterpret_cast<char*>(&loginRsp), sizeof(loginRsp));

    if (loginRsp.code == 0)
        sendGatewayInfo(connID, 0, "OK");
    else
        sendGatewayInfo(connID, -1, "Login failed");
}

void LoginServer::sendGatewayInfo(ConnID connID, int32_t code, const char* msg)
{
    Msg_S2C_GatewayInfo info{};
    info.code = code;
    copyToWire(info.msg, sizeof(info.msg), msg);
    if (code == 0)
    {
        LoginGatewayEntry gw;
        if (m_gatewayRegistry.pickRoundRobin(gw))
        {
            copyToWire(info.gatewayIP, sizeof(info.gatewayIP), gw.ip.c_str());
            info.gatewayPort = gw.port;
        }
        else
        {
            info.code = -1;
            copyToWire(info.msg, sizeof(info.msg), "No gateway available");
        }
    }
    m_clientServer.SendMsg(connID, (uint16_t)ClientMsgID::S2C_GATEWAY_INFO,
                           reinterpret_cast<char*>(&info), sizeof(info));
    LOG_INFO("Sent gateway info: conn=%u code=%d ip=%s port=%u",
             connID, info.code, info.gatewayIP, info.gatewayPort);
}

void LoginServer::pruneGatewayTable()
{
    const uint64_t before = m_gatewayRegistry.size();
    m_gatewayRegistry.pruneStale(TimerMgr::NowMs());
    const uint64_t after = m_gatewayRegistry.size();
    if (after < before)
        LOG_INFO("Pruned stale gateways: %llu -> %llu",
                 static_cast<unsigned long long>(before),
                 static_cast<unsigned long long>(after));
}
