/**
 * @file GlobalServer.cpp
 * @brief GlobalServer 非简单成员函数实现，降低头文件编译耦合。
 */

#include "GlobalServer.h"

#include <algorithm>

GlobalServer::GlobalServer() : m_server(this) {}

bool GlobalServer::Init(const std::string& ip, uint16_t port)
{
    Logger::Instance().SetServerName("GlobalServer");
    if (!m_server.Start(ip, port)) { LOG_FATAL("GlobalServer start failed"); return false; }
    RegisterHandlers();
    TimerMgr::Instance().Register(60000, 60000, [this] { SyncGlobalData(); });
    LOG_INFO("GlobalServer started on %s:%d", ip.c_str(), port);
    return true;
}

void GlobalServer::Run()
{
    while (true) { m_server.Poll(10); TimerMgr::Instance().Update(); }
}

void GlobalServer::OnConnect(ConnID id) { LOG_DEBUG("GlobalServer conn=%u", id); }

void GlobalServer::OnDisconnect(ConnID id) { LOG_WARN("GlobalServer conn=%u lost", id); }

void GlobalServer::OnMessage(ConnID id, uint8_t module, uint8_t sub,
                             const char* data, uint16_t len)
{
    MsgDispatcher::Instance().Dispatch(id, module, sub, data, len);
}

void GlobalServer::RegisterHandlers()
{
    auto& d = MsgDispatcher::Instance();
    d.Register((uint16_t)InternalMsgID::GLB_RANK_UPDATE,
               [this](uint32_t c, const char* data, uint16_t len) { OnRankUpdate(c, data, len); });
    d.Register((uint16_t)InternalMsgID::GLB_DATA_SYNC,
               [this](uint32_t c, const char* data, uint16_t len) { OnDataSync(c, data, len); });
}

void GlobalServer::OnRankUpdate(ConnID /*fromConn*/, const char* data, uint16_t len)
{
    if (len < sizeof(RankEntry)) return;
    const auto* entry = reinterpret_cast<const RankEntry*>(data);
    m_rank.push_back(*entry);
    std::sort(m_rank.begin(), m_rank.end(),
              [](const RankEntry& a, const RankEntry& b) { return a.value > b.value; });
    if (m_rank.size() > 100) m_rank.resize(100);
}

void GlobalServer::OnDataSync(ConnID /*fromConn*/, const char* data, uint16_t len)
{
    LOG_DEBUG("GlobalDataSync len=%d", len);
    for (auto& [cid, alive] : m_innerConns)
    {
        (void)alive;
        m_server.SendMsg(cid, (uint16_t)InternalMsgID::GLB_DATA_SYNC, data, len);
    }
}

void GlobalServer::SyncGlobalData()
{
    LOG_INFO("GlobalServer: syncing rank to all scene servers. rank size=%zu", m_rank.size());
    // TODO: 序列化排行榜数据广播给各 SceneServer
}
