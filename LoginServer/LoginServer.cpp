/**
 * @file    LoginServer.cpp
 * @brief   LoginServer 实现
 */

#include "LoginServer.h"
#include "LoginGameZoneMsg.h"
#include "LoginClientMsgRegister.h"
#include "ClientCommon.pb.h"
#include "LoginCommon.pb.h"
#include "LoginMsg.pb.h"
#include "../sdk/net/MsgIngress.h"
#include "../sdk/net/NetTls.h"
#include "../sdk/net/ClientWireSend.h"
#include "../sdk/timer/TimerMgr.h"
#include "../sdk/math/Random.h"
#include "../sdk/util/PasswordDigestUtil.h"

#include <cstdio>
#include <cstring>

namespace
{

constexpr uint8_t kLoginModule = static_cast<uint8_t>(rpg::client::LOGIN);

} // namespace

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
        IngressContext ctx{};
        ctx.kind = ConnKind::ClientWire;
        ctx.connId = id;
        ctx.module = module;
        ctx.sub = sub;
        ctx.data = data;
        ctx.len = len;
        MsgIngress::onMessage(ctx);
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
        MsgIngress::dispatchInternal(id, module, sub, data, len);
    }

    LoginServer* m_owner;
};

LoginServer::LoginServer()
    : m_clientBridge(std::make_unique<ClientPortBridge>(this))
    , m_registerBridge(std::make_unique<RegisterPortBridge>(this))
    , m_clientServer(m_clientBridge.get())
    , m_registerServer(m_registerBridge.get())
    , m_authService(*this)
    , m_registerService(*this)
    , m_rechargeService(*this)
    , m_gmService(*this)
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
        LOG_FATAL("登录服初始化数据库句柄失败");
        return false;
    }
    if (!mysql_real_connect(m_db, dbCfg.host.c_str(), dbCfg.user.c_str(), dbCfg.pass.c_str(),
                            dbCfg.name.c_str(), static_cast<unsigned int>(dbCfg.port),
                            nullptr, 0))
    {
        LOG_FATAL("登录服连接数据库失败: %s", mysql_error(m_db));
        mysql_close(m_db);
        m_db = nullptr;
        return false;
    }
    mysql_set_character_set(m_db, "utf8mb4");
    LOG_INFO("登录服数据库连接成功: %s:%d/%s",
             dbCfg.host.c_str(), dbCfg.port, dbCfg.name.c_str());
    return true;
}

bool LoginServer::loadServerList(const std::string& path)
{
    if (path.empty())
    {
        LOG_FATAL("登录服: 服务器列表路径为空");
        return false;
    }
    if (!m_zoneInfoStore.loadFromFile(path.c_str()))
    {
        LOG_FATAL("登录服加载服务器列表失败: %s", path.c_str());
        return false;
    }
    return true;
}

bool LoginServer::Init(const LoginExternConfig& cfg)
{
    Logger::Instance().SetServerName("LoginServer");
    if (cfg.clientListenPort == 0 || cfg.registerListenPort == 0)
    {
        LOG_FATAL("登录服: 必须配置客户端监听/注册监听端口");
        return false;
    }
    if (!loadServerList(cfg.serverListPath))
        return false;
    if (!initDatabase(cfg.database))
        return false;

    wireTlsServer(m_clientServer, false);
    wireTlsServer(m_registerServer, true);

    if (!m_clientServer.Start(cfg.clientListenIP, cfg.clientListenPort))
    {
        LOG_FATAL("登录服客户端监听失败: %s:%u",
                  cfg.clientListenIP.c_str(), cfg.clientListenPort);
        return false;
    }
    if (!m_registerServer.Start(cfg.registerListenIP, cfg.registerListenPort))
    {
        LOG_FATAL("登录服注册监听失败: %s:%u",
                  cfg.registerListenIP.c_str(), cfg.registerListenPort);
        return false;
    }

    registerHandlers();
    TimerMgr::Instance().Register(10000, 10000, [this] { pruneGatewayTable(); });
    LOG_INFO("登录服启动成功: client=%s:%u register=%s:%u zones=%zu",
             cfg.clientListenIP.c_str(), cfg.clientListenPort,
             cfg.registerListenIP.c_str(), cfg.registerListenPort,
             m_zoneInfoStore.size());
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
    LOG_INFO("登录客户端连接: conn=%u", id);
    sendLoginChallenge(id);
}

void LoginServer::sendLoginChallenge(ConnID connId)
{
    std::string nonce;
    nonce.resize(LOGIN_CHALLENGE_NONCE_LEN);
    for (size_t i = 0; i < LOGIN_CHALLENGE_NONCE_LEN; ++i)
        nonce[i] = static_cast<char>(Random::rangeU(0, 255));

    m_loginChallengeNonces[connId] = nonce;

    rpg::login::S2CLoginChallenge challenge;
    challenge.set_nonce(nonce);
    if (!sendClientProtoModule(m_clientServer, connId, kLoginModule,
                               static_cast<uint8_t>(rpg::login::S2C_LOGIN_CHALLENGE),
                               challenge))
    {
        LOG_WARN("登录挑战下发失败: conn=%u", connId);
        m_loginChallengeNonces.erase(connId);
        return;
    }
    LOG_DEBUG("已下发登录挑战: conn=%u nonceLen=%zu", connId, nonce.size());
}

bool LoginServer::peekLoginChallengeNonce(ConnID connId, const std::string& loginNonce) const
{
    auto it = m_loginChallengeNonces.find(connId);
    if (it == m_loginChallengeNonces.end())
        return false;
    return it->second.size() == LOGIN_CHALLENGE_NONCE_LEN &&
           loginNonce.size() == LOGIN_CHALLENGE_NONCE_LEN &&
           it->second == loginNonce;
}

bool LoginServer::verifyAndConsumeLoginNonce(ConnID connId, const std::string& loginNonce)
{
    auto it = m_loginChallengeNonces.find(connId);
    if (it == m_loginChallengeNonces.end())
        return false;
    if (it->second.size() != LOGIN_CHALLENGE_NONCE_LEN ||
        loginNonce.size() != LOGIN_CHALLENGE_NONCE_LEN ||
        it->second != loginNonce)
    {
        return false;
    }
    m_loginChallengeNonces.erase(it);
    return true;
}

void LoginServer::onClientDisconnect(ConnID id)
{
    m_loginChallengeNonces.erase(id);
    if (!m_clientServer.connectNotified(id))
    {
        LOG_WARN("登录客户端 TLS 握手未完成即断开: conn=%u（客户端可能仍用明文 TCP）",
                 id);
        return;
    }
    LOG_INFO("登录客户端断开: conn=%u", id);
}

void LoginServer::registerHandlers()
{
    LoginClientMsgRegister(*this);
    LoginGameZoneMsgRegister(*this);
}

void LoginServer::onRegisterConnect(ConnID id)
{
    LOG_INFO("网关注册连接建立: conn=%u", id);
}

void LoginServer::onRegisterDisconnect(ConnID id)
{
    LOG_INFO("网关注册连接断开: conn=%u", id);
}

void LoginServer::pruneGatewayTable()
{
    const uint64_t before = m_gatewayRegistry.size();
    m_gatewayRegistry.pruneStale(TimerMgr::NowMs());
    const uint64_t after = m_gatewayRegistry.size();
    if (after < before)
        LOG_INFO("清理过期网关: %llu -> %llu",
                 static_cast<unsigned long long>(before),
                 static_cast<unsigned long long>(after));
}
