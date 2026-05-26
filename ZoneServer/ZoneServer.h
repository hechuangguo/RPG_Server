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
 *
 * ## 跨区转发协议路由说明
 *
 * ZoneServer 作为跨区消息的中转枢纽，核心职责是将来自一个游戏区的请求
 * 准确路由到目标游戏区。以下是完整的协议路由流程：
 *
 * ### 1. 路由表建立
 *
 * 各游戏区的 SceneServer / SessionServer 启动时连接到 ZoneServer，
 * 并通过内部注册协议上报自己的 zoneID。ZoneServer 将连接信息存入 m_routes 路由表：
 *
 *     zoneID → ZoneRoute { connID, alive }
 *
 * 当连接断开时（OnDisconnect），自动将对应路由标记为 alive=false，
 * 防止向已失效的连接转发消息。
 *
 * ### 2. 跨区请求协议（ZONE_CROSS_REQ）
 *
 * 源区服发送跨区请求，包格式如下：
 *
 *     [dstZoneID: uint32_t][userID: uint64_t][payload: 可变长度]
 *
 * - dstZoneID: 目标区服的 zoneID，ZoneServer 据此查路由表。
 * - userID: 发起请求的用户 ID，用于目标区服定位用户数据。
 * - payload: 具体的跨区业务数据（如邮件内容、好友申请、组队邀请等）。
 *
 * ZoneServer 收到后：
 *   a) 从 m_routes 查找 dstZoneID 对应的 connID。
 *   b) 若路由存在且 alive=true，将整个消息包转发给目标连接（ZONE_FORWARD）。
 *   c) 向源连接回发 ZONE_CROSS_RSP（code=0 表示转发成功）。
 *   d) 若路由不存在或已失效，仅记录警告日志，不回发响应。
 *
 * ### 3. 跨区转发协议（ZONE_FORWARD）
 *
 * 目标区服的 SceneServer / SessionServer 收到 ZONE_FORWARD 消息后，
 * 根据 payload 中的业务类型进行相应处理，处理结果通过类似路径回传。
 *
 * ### 4. 路由安全性
 *
 * - ZoneServer 不校验 payload 内容，仅负责路由转发。
 * - 若需安全加固，可在 OnCrossReq 中增加来源 zoneID 校验、消息签名验证等。
 * - 连接断开后 alive 标记防止向死连接写入，但不会自动清理路由条目。
 *   如果目标区服重连，应发送注册消息覆盖旧路由条目。
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
     *
     * 遍历所有路由条目，将 connID 匹配的条目标记为 alive=false。
     * 这样后续的跨区请求不会向已断开的连接转发消息。
     */
    void OnDisconnect(ConnID id) override
    {
        LOG_WARN("Zone conn=%u lost", id);
        for (auto& [zid, r] : m_routes) if (r.connID == id) r.alive = false;
    }

    void OnMessage(ConnID id, uint8_t module, uint8_t sub,
                   const char* data, uint16_t len) override
    {
        MsgDispatcher::Instance().Dispatch(id, module, sub, data, len);
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
     * 包格式：[dstZoneID(4)][userID(8)][payload...]
     *
     * 路由流程：
     * 1. 解析前 4 字节获取目标区服 ID（dstZoneID）。
     * 2. 在 m_routes 路由表中查找该 zoneID。
     * 3. 若路由存在且连接存活，将整个原始消息包（含 dstZoneID + userID + payload）
     *    以 ZONE_FORWARD 消息 ID 转发给目标连接。
     * 4. 向源连接发送 ZONE_CROSS_RSP 响应，code=0 表示转发成功。
     * 5. 若路由不存在或已失效，仅打印警告日志。
     *
     * 注意：ZoneServer 不修改 payload 内容，保持透传。
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

    /**
     * @brief 处理跨区转发消息（当前为占位实现）
     *
     * 目标区服的 SceneServer/SessionServer 收到 ZONE_FORWARD 后，
     * 会根据 payload 中的业务数据进行处理。
     * 此处为 ZoneServer 自身收到 FORWARD 的占位逻辑（正常情况下 ZoneServer 不应收到 FORWARD）。
     */
    void OnForward(ConnID /*fromConn*/, const char* /*data*/, uint16_t len)
    {
        LOG_DEBUG("ZoneForward len=%d", len);
    }

    TcpServer m_server;  /**< 监听内部连接 */

    /**
     * @brief 跨区路由表：zoneID → ZoneRoute
     *
     * 每个 ZoneRoute 记录了目标区服的 zoneID、对应的连接 ID 以及连接存活状态。
     * 路由表在区服连接建立时写入，连接断开时标记为失效（alive=false）。
     * 若目标区服重连并发送注册消息，应覆盖旧路由条目。
     */
    std::unordered_map<ZoneID, ZoneRoute> m_routes;
};
