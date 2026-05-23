#pragma once
#include "../sdk/net/TcpServer.h"
#include "../sdk/net/TcpClient.h"
#include "../sdk/util/RoleBase.h"
#include "../sdk/util/MsgDispatcher.h"
#include "../sdk/log/Logger.h"
#include "../sdk/timer/TimerMgr.h"
#include "../protocal/InternalMsg.h"
#include <unordered_map>
#include <string>
#include <vector>

// ============================================================
//  GlobalServer —— 全区数据管理（排行榜/全服公告等），可选
// ============================================================

struct RankEntry
{
    RoleID   roleID;
    char     name[32];
    uint32_t value;
};

class GlobalServer : public INetCallback
{
public:
    GlobalServer() : m_server(this) {}

    bool Init(const std::string& ip, uint16_t port)
    {
        Logger::Instance().SetServerName("GlobalServer");
        if (!m_server.Start(ip, port)) { LOG_FATAL("GlobalServer start failed"); return false; }
        RegisterHandlers();
        TimerMgr::Instance().Register(60000, 60000, [this]{ SyncGlobalData(); });
        LOG_INFO("GlobalServer started on %s:%d", ip.c_str(), port);
        return true;
    }

    void Run()
    {
        while (true) { m_server.Poll(10); TimerMgr::Instance().Update(); }
    }

    void OnConnect(ConnID id)    override { LOG_DEBUG("GlobalServer conn=%u", id); }
    void OnDisconnect(ConnID id) override { LOG_WARN("GlobalServer conn=%u lost", id); }
    void OnMessage(ConnID id, uint16_t msgID, const char* data, uint16_t len) override
    {
        MsgDispatcher::Instance().Dispatch(id, msgID, data, len);
    }

private:
    void RegisterHandlers()
    {
        auto& d = MsgDispatcher::Instance();
        d.Register((uint16_t)InternalMsgID::GLB_RANK_UPDATE,
            [this](uint32_t c, const char* d, uint16_t l){ OnRankUpdate(c, d, l); });
        d.Register((uint16_t)InternalMsgID::GLB_DATA_SYNC,
            [this](uint32_t c, const char* d, uint16_t l){ OnDataSync(c, d, l); });
    }

    void OnRankUpdate(ConnID fromConn, const char* data, uint16_t len)
    {
        if (len < sizeof(RankEntry)) return;
        const auto* entry = reinterpret_cast<const RankEntry*>(data);
        // 更新排行榜（简化：最多保留前 100 名）
        m_rank.push_back(*entry);
        std::sort(m_rank.begin(), m_rank.end(),
                  [](const RankEntry& a, const RankEntry& b){ return a.value > b.value; });
        if (m_rank.size() > 100) m_rank.resize(100);
    }

    void OnDataSync(ConnID fromConn, const char* data, uint16_t len)
    {
        LOG_DEBUG("GlobalDataSync len=%d", len);
        // 广播给所有连接的 SceneServer
        for (auto& [cid, _] : m_innerConns)
            m_server.SendMsg(cid, (uint16_t)InternalMsgID::GLB_DATA_SYNC, data, len);
    }

    void SyncGlobalData()
    {
        LOG_INFO("GlobalServer: syncing rank to all scene servers. rank size=%zu", m_rank.size());
        // 序列化排行榜数据广播
    }

    TcpServer m_server;
    std::vector<RankEntry>              m_rank;
    std::unordered_map<ConnID, bool>    m_innerConns;
};
