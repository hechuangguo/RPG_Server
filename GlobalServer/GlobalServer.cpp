/**
 * @file GlobalServer.cpp
 * @brief GlobalServer 非简单成员函数实现，降低头文件编译耦合。
 */

#include "GlobalServer.h"
#include "../sdk/net/MsgIngress.h"

#include "GlobalHttpApi.h"
#include "GlobalGameZoneMsg.h"
#include "../sdk/http/HttpCodec.h"

#include <algorithm>
#include <cstdio>

GlobalServer::GlobalServer() : m_server(this) {}

GlobalServer::~GlobalServer()
{
    if (m_db)
    {
        mysql_close(m_db);
        m_db = nullptr;
    }
}

bool GlobalServer::initDatabase(const DatabaseConfig& dbCfg)
{
    if (!dbCfg.configured)
        return true;
    m_db = mysql_init(nullptr);
    if (!m_db)
    {
        LOG_FATAL("全局服初始化 MySQL 句柄失败");
        return false;
    }
    if (!mysql_real_connect(m_db, dbCfg.host.c_str(), dbCfg.user.c_str(), dbCfg.pass.c_str(),
                            dbCfg.name.c_str(), static_cast<unsigned int>(dbCfg.port),
                            nullptr, 0))
    {
        LOG_FATAL("全局服连接 MySQL 失败: %s", mysql_error(m_db));
        mysql_close(m_db);
        m_db = nullptr;
        return false;
    }
    mysql_set_character_set(m_db, "utf8mb4");
    LOG_INFO("全局服 MySQL 连接成功: %s:%d/%s",
             dbCfg.host.c_str(), dbCfg.port, dbCfg.name.c_str());
    return true;
}

bool GlobalServer::Init(const ExternServerConfig& cfg)
{
    Logger::Instance().SetServerName("GlobalServer");
    if (!m_server.Start(cfg.listenIP, cfg.listenPort))
    {
        LOG_FATAL("全局服启动失败");
        return false;
    }
    if (!initDatabase(cfg.database))
        return false;

    if (cfg.httpListen.port > 0)
    {
        m_httpServer.setHandler([this](ConnID id, const HttpRequest& req) {
            return onHttpRequest(id, req);
        });
        if (!m_httpServer.start(cfg.httpListen.listenIP, cfg.httpListen.port))
        {
            LOG_FATAL("全局服网页接口监听失败: %s:%u",
                      cfg.httpListen.listenIP.c_str(), cfg.httpListen.port);
            return false;
        }
        LOG_INFO("全局服网页接口监听中: %s:%u",
                 cfg.httpListen.listenIP.c_str(), cfg.httpListen.port);
    }

    m_httpClient.configure(cfg.httpClient);
    if (cfg.httpClient.enabled)
        m_httpClient.connectIfConfigured();

    RegisterHandlers();
    TimerMgr::Instance().Register(60000, 60000, [this] { SyncGlobalData(); });
    if (cfg.httpClient.enabled && cfg.httpClient.port > 0)
    {
        TimerMgr::Instance().Register(30000, 30000, [this] { probeHttpPeer(); });
    }
    LOG_INFO("全局服启动完成: %s:%u", cfg.listenIP.c_str(), cfg.listenPort);
    return true;
}

void GlobalServer::Run()
{
    while (true)
    {
        m_server.Poll(10);
        m_httpServer.poll(0);
        m_httpClient.poll(0);
        m_httpClient.tickReconnect(TimerMgr::NowMs());
        TimerMgr::Instance().Update();
    }
}

void GlobalServer::OnConnect(ConnID id)
{
    m_innerConns[id] = true;
    LOG_DEBUG("全局服连接建立: conn=%u", id);
}

void GlobalServer::OnDisconnect(ConnID id)
{
    m_innerConns.erase(id);
    LOG_WARN("全局服连接断开: conn=%u", id);
}

void GlobalServer::OnMessage(ConnID id, uint8_t module, uint8_t sub,
                             const char* data, uint16_t len)
{
    MsgIngress::dispatchInternal(id, module, sub, data, len);
}

void GlobalServer::RegisterHandlers()
{
    GlobalGameZoneMsgRegister(*this);
}

std::string GlobalServer::onHttpRequest(ConnID /*connId*/, const HttpRequest& req)
{
    const HttpApiResponse api = GlobalHttpApi::dispatch(req, m_db, m_rank);
    return HttpCodec::buildJsonResponse(api.status, api.reason, api.jsonBody);
}

void GlobalServer::OnRankUpdate(ConnID /*fromConn*/, const char* data, uint16_t len)
{
    if (len < sizeof(RankEntry))
        return;
    const auto* entry = reinterpret_cast<const RankEntry*>(data);
    m_rank.push_back(*entry);
    std::sort(m_rank.begin(), m_rank.end(),
              [](const RankEntry& a, const RankEntry& b) { return a.value > b.value; });
    if (m_rank.size() > 100)
        m_rank.resize(100);
}

void GlobalServer::OnDataSync(ConnID /*fromConn*/, const char* data, uint16_t len)
{
    LOG_DEBUG("全局数据同步: len=%d", len);
    for (auto& [cid, alive] : m_innerConns)
    {
        (void)alive;
        m_server.SendMsg(cid, (uint16_t)InternalMsgID::GLB_DATA_SYNC, data, len);
    }
}

void GlobalServer::SyncGlobalData()
{
    LOG_INFO("全局服开始同步排行到所有场景服，排行数量=%zu", m_rank.size());
}

void GlobalServer::probeHttpPeer()
{
    if (!m_httpClient.isConfigured())
        return;
    if (!m_httpClient.isConnected())
        m_httpClient.connectIfConfigured();
    if (m_httpClient.isConnected())
        m_httpClient.sendGet("/health");
}
