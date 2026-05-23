/**
 * @file    ZoneServer.h
 * @brief  跨区服务器 —— 跨游戏区数据转发与逻辑处理，可选启动
 *
 * ## 职责
 * - 维护各游戏区的路由表（zoneID → connID）
 * - 跨区消息转发（ZONE_CROSS_REQ → ZONE_FORWARD）
 * - 跨区玩家数据交换
 *
 * ## 特性
 * - 可选服务（通过环境变量 ENABLE_ZONE=1 控制是否启动）
 * - 所有游戏区共享一个 ZoneServer 进程
 * - 不依赖其他服务器，独立监听
 *
 * ## 典型流程
 * @code
 *   SceneServer(A区) → ZoneServer → SceneServer(B区)
 *   SessionServer(A区) → ZoneServer → SessionServer(B区)
 * @endcode
 */

#pragma once
#include "../sdk/net/TcpServer.h"
#include "../sdk/net/TcpClient.h"
#include "../sdk/util/MsgDispatcher.h"
#include "../sdk/log/Logger.h"
#include "../sdk/timer/TimerMgr.h"
#include "../protocal/InternalMsg.h"
#include <unordered_map>
#include <string>

/** @brief 区服唯一标识 */
using ZoneID = uint32_t;

/**
 * @brief 跨区路由条目
 */
struct ZoneRoute
{
    ZoneID  zoneID;  /**< 目标区服 ID */
    ConnID  connID;  /**< 该区服的内部连接 ID */
    bool    alive;   /**< 是否存活 */
};

/**
 * @brief ZoneServer 核心类
 *
 * 单进程运行，维护所有游戏区之间的路由转发。
 */
class ZoneServer : public INetCallback
{
public:
    ZoneServer() : m_server(this) {}

    /**
     * @brief 初始化 ZoneServer
     * @param ip   监听 IP
     * @param port 监听端口
     * @return 成功返回 true
     */
    bool Init(const std::string& ip, uint16_t port)
    {
        Logger::Instance().SetServerName("ZoneServer");
        if (!m_server.Start(ip, port)) { LOG_FATAL("ZoneServer start failed"); return false; }
        RegisterHandlers();
        LOG_INFO("ZoneServer started on %s:%d", ip.c_str(), port);
        return true;
    }

    /** @brief 主循环 */
    void Run()
    {
        while (true) { m_server.Poll(10); TimerMgr::Instance().Update(); }
    }

    void OnConnect(ConnID id)    override { LOG_INFO("Zone conn=%u", id); }

    /**
     * @brief 连接断开时标记路由为失效
     */
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

    /**
     * @brief 处理跨区请求
     *
     * 包格式：[dstZoneID(4)][roleID(8)][payload...]
     * 查找目标区服路由 → 转发 → 回包确认。
     */
    void OnCrossReq(ConnID fromConn, const char* data, uint16_t len)
    {
        if (len < 12) return;
        ZoneID dstZone = *reinterpret_cast<const ZoneID*>(data);
        LOG_INFO("CrossReq: dstZone=%u from conn=%u", dstZone, fromConn);
        auto it = m_routes.find(dstZone);
        if (it != m_routes.end() && it->second.alive)
        {
            m_server.SendMsg(it->second.connID,
                             (uint16_t)InternalMsgID::ZONE_FORWARD, data, len);
            char rsp[8] = {0};  /**< code=0 表示转发成功 */
            m_server.SendMsg(fromConn, (uint16_t)InternalMsgID::ZONE_CROSS_RSP, rsp, sizeof(rsp));
        }
        else
        {
            LOG_WARN("CrossReq: dstZone=%u not found", dstZone);
        }
    }

    /** @brief 处理跨区转发消息（当前为占位实现） */
    void OnForward(ConnID fromConn, const char* data, uint16_t len)
    {
        LOG_DEBUG("ZoneForward len=%d", len);
    }

    TcpServer m_server;  /**< 监听内部连接 */

    /** @brief 跨区路由表：zoneID → ZoneRoute */
    std::unordered_map<ZoneID, ZoneRoute> m_routes;
};
