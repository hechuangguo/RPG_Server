#pragma once
#include "../sdk/net/TcpServer.h"
#include "../sdk/net/TcpClient.h"
#include "../sdk/util/MsgDispatcher.h"
#include "../sdk/log/Logger.h"
#include "../sdk/timer/TimerMgr.h"
#include "../protocal/InternalMsg.h"
#include <unordered_map>
#include <string>

// ============================================================
//  ZoneServer —— 跨区数据转发，跨区逻辑处理，可选
// ============================================================

// 区服标识
using ZoneID = uint32_t;

struct ZoneRoute
{
    ZoneID  zoneID;
    ConnID  connID;  // 对应的连接
    bool    alive;
};

class ZoneServer : public INetCallback
{
public:
    ZoneServer() : m_server(this) {}

    bool Init(const std::string& ip, uint16_t port)
    {
        Logger::Instance().SetServerName("ZoneServer");
        if (!m_server.Start(ip, port)) { LOG_FATAL("ZoneServer start failed"); return false; }
        RegisterHandlers();
        LOG_INFO("ZoneServer started on %s:%d", ip.c_str(), port);
        return true;
    }

    void Run()
    {
        while (true) { m_server.Poll(10); TimerMgr::Instance().Update(); }
    }

    void OnConnect(ConnID id)    override { LOG_INFO("Zone conn=%u", id); }
    void OnDisconnect(ConnID id) override
    {
        LOG_WARN("Zone conn=%u lost", id);
        for (auto& [zid, r] : m_routes) if (r.connID == id) r.alive = false;
    }
    void OnMessage(ConnID id, uint16_t msgID, const char* data, uint16_t len) override
    {
        MsgDispatcher::Instance().Dispatch(id, msgID, data, len);
    }

private:
    void RegisterHandlers()
    {
        auto& d = MsgDispatcher::Instance();
        d.Register((uint16_t)InternalMsgID::ZONE_CROSS_REQ,
            [this](uint32_t c, const char* d, uint16_t l){ OnCrossReq(c, d, l); });
        d.Register((uint16_t)InternalMsgID::ZONE_FORWARD,
            [this](uint32_t c, const char* d, uint16_t l){ OnForward(c, d, l); });
    }

    void OnCrossReq(ConnID fromConn, const char* data, uint16_t len)
    {
        // 跨区请求：data = [dstZoneID(4)][roleID(8)][payload...]
        if (len < 12) return;
        ZoneID dstZone = *reinterpret_cast<const ZoneID*>(data);
        LOG_INFO("CrossReq: dstZone=%u from conn=%u", dstZone, fromConn);
        auto it = m_routes.find(dstZone);
        if (it != m_routes.end() && it->second.alive)
        {
            // 转发
            m_server.SendMsg(it->second.connID,
                             (uint16_t)InternalMsgID::ZONE_FORWARD, data, len);
            // 回包
            char rsp[8] = {0}; // code=0
            m_server.SendMsg(fromConn, (uint16_t)InternalMsgID::ZONE_CROSS_RSP, rsp, sizeof(rsp));
        }
        else
        {
            LOG_WARN("CrossReq: dstZone=%u not found", dstZone);
        }
    }

    void OnForward(ConnID fromConn, const char* data, uint16_t len)
    {
        // 跨区转发消息到目标区服的 SessionServer
        LOG_DEBUG("ZoneForward len=%d", len);
    }

    TcpServer m_server;
    std::unordered_map<ZoneID, ZoneRoute> m_routes;
};
