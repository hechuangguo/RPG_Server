/**
 * @file ZoneServer.cpp
 * @brief ZoneServer 非简单成员函数实现，降低头文件编译耦合。
 */

#include "ZoneServer.h"
#include "ZoneInternMsgRegister.h"
#include "../sdk/net/MsgIngress.h"

ZoneServer::ZoneServer() : m_server(this) {}

ZoneServer::~ZoneServer()
{
    if (m_db)
    {
        mysql_close(m_db);
        m_db = nullptr;
    }
}

bool ZoneServer::initDatabase(const DatabaseConfig& dbCfg)
{
    if (!dbCfg.configured)
        return true;
    m_db = mysql_init(nullptr);
    if (!m_db)
    {
        LOG_FATAL("跨区服初始化 MySQL 句柄失败");
        return false;
    }
    if (!mysql_real_connect(m_db, dbCfg.host.c_str(), dbCfg.user.c_str(), dbCfg.pass.c_str(),
                            dbCfg.name.c_str(), static_cast<unsigned int>(dbCfg.port),
                            nullptr, 0))
    {
        LOG_FATAL("跨区服连接 MySQL 失败: %s", mysql_error(m_db));
        mysql_close(m_db);
        m_db = nullptr;
        return false;
    }
    mysql_set_character_set(m_db, "utf8mb4");
    LOG_INFO("跨区服 MySQL 连接成功: %s:%d/%s",
             dbCfg.host.c_str(), dbCfg.port, dbCfg.name.c_str());
    return true;
}

bool ZoneServer::Init(const ExternServerConfig& cfg)
{
    Logger::Instance().SetServerName("ZoneServer");
    if (!m_server.Start(cfg.listenIP, cfg.listenPort))
    {
        LOG_FATAL("跨区服启动失败");
        return false;
    }
    if (!initDatabase(cfg.database))
        return false;
    registerHandlers();
    LOG_INFO("跨区服启动完成: %s:%u", cfg.listenIP.c_str(), cfg.listenPort);
    return true;
}

void ZoneServer::Run()
{
    while (true)
    {
        m_server.Poll(10);
        TimerMgr::Instance().Update();
    }
}

void ZoneServer::OnConnect(ConnID id) { LOG_INFO("跨区服连接建立: conn=%u", id); }

void ZoneServer::OnDisconnect(ConnID id)
{
    LOG_WARN("跨区服连接断开: conn=%u", id);
    for (auto& [zid, route] : m_routes)
    {
        (void)zid;
        if (route.connID == id)
            route.alive = false;
    }
}

void ZoneServer::OnMessage(ConnID id, uint8_t module, uint8_t sub,
                           const char* data, uint16_t len)
{
    MsgIngress::dispatchInternal(id, module, sub, data, len);
}

void ZoneServer::registerHandlers()
{
    ZoneInternMsgRegister(*this);
}

void ZoneServer::onCrossReq(ConnID fromConn, const char* data, uint16_t len)
{
    if (len < 12)
        return;
    ZoneID dstZone = *reinterpret_cast<const ZoneID*>(data);
    LOG_INFO("跨区请求: dstZone=%u from conn=%u", dstZone, fromConn);
    auto it = m_routes.find(dstZone);
    if (it != m_routes.end() && it->second.alive)
    {
        m_server.SendMsg(it->second.connID,
                         (uint16_t)InternalMsgID::ZONE_FORWARD, data, len);
        char rsp[8] = {0};
        m_server.SendMsg(fromConn, (uint16_t)InternalMsgID::ZONE_CROSS_RSP, rsp, sizeof(rsp));
    }
    else
    {
        LOG_WARN("跨区请求失败: dstZone=%u 未找到", dstZone);
    }
}

void ZoneServer::onForward(ConnID /*fromConn*/, const char* /*data*/, uint16_t len)
{
    LOG_DEBUG("跨区转发: len=%d", len);
}
