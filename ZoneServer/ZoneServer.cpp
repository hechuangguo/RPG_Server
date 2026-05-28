/**
 * @file ZoneServer.cpp
 * @brief ZoneServer 非简单成员函数实现，降低头文件编译耦合。
 */

#include "ZoneServer.h"

ZoneServer::ZoneServer() : m_server(this) {}

bool ZoneServer::Init(const std::string& ip, uint16_t port)
{
    Logger::Instance().SetServerName("ZoneServer");
    if (!m_server.Start(ip, port)) { LOG_FATAL("ZoneServer start failed"); return false; }
    RegisterHandlers();
    LOG_INFO("ZoneServer started on %s:%d", ip.c_str(), port);
    return true;
}

void ZoneServer::Run()
{
    while (true) { m_server.Poll(10); TimerMgr::Instance().Update(); }
}

void ZoneServer::OnConnect(ConnID id) { LOG_INFO("Zone conn=%u", id); }

void ZoneServer::OnDisconnect(ConnID id)
{
    LOG_WARN("Zone conn=%u lost", id);
    for (auto& [zid, route] : m_routes)
    {
        (void)zid;
        if (route.connID == id) route.alive = false;
    }
}

void ZoneServer::OnMessage(ConnID id, uint8_t module, uint8_t sub,
                           const char* data, uint16_t len)
{
    MsgDispatcher::Instance().Dispatch(id, module, sub, data, len);
}

void ZoneServer::RegisterHandlers()
{
    auto& d = MsgDispatcher::Instance();
    d.Register((uint16_t)InternalMsgID::ZONE_CROSS_REQ,
               [this](uint32_t c, const char* data, uint16_t len) { OnCrossReq(c, data, len); });
    d.Register((uint16_t)InternalMsgID::ZONE_FORWARD,
               [this](uint32_t c, const char* data, uint16_t len) { OnForward(c, data, len); });
}

void ZoneServer::OnCrossReq(ConnID fromConn, const char* data, uint16_t len)
{
    if (len < 12) return;
    ZoneID dstZone = *reinterpret_cast<const ZoneID*>(data);
    LOG_INFO("CrossReq: dstZone=%u from conn=%u", dstZone, fromConn);
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
        LOG_WARN("CrossReq: dstZone=%u not found", dstZone);
    }
}

void ZoneServer::OnForward(ConnID /*fromConn*/, const char* /*data*/, uint16_t len)
{
    LOG_DEBUG("ZoneForward len=%d", len);
}
