/**
 * @file    SceneClient.h
 * @brief   Gateway 到单个 SceneServer 实例的出站连接
 *
 * 封装 TcpClient 与 GW_CLIENT_MSG 转发，不含业务路由逻辑。
 */

#pragma once

#include "../sdk/net/TcpClient.h"
#include "../protocal/InternalMsg.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

/**
 * @brief Gateway → 单个 SceneServer 的出站连接
 */
class SceneClient
{
public:
    /**
     * @brief 构造 SceneClient
     * @param sceneServerId ServerList 中的 server_id
     * @param cb 出站 INetCallback（通常为 GatewayUpstreamCallback）
     */
    SceneClient(uint32_t sceneServerId, INetCallback* cb);

    /** @brief SceneServer 实例 ID */
    uint32_t sceneServerId() const { return m_sceneServerId; }

    /** @brief 连接目标 SceneServer */
    bool connect(const std::string& ip, uint16_t port);

    /** @brief 驱动 epoll */
    void poll();

    /** @brief 是否已连接 */
    bool isConnected() const;

    /** @brief 底层 TcpClient（兼容现有 clientFor 指针 API） */
    TcpClient* tcpClient() { return m_client.get(); }

    /**
     * @brief 转发客户端上行消息
     * @param clientConnId Gateway 侧客户端连接 ID
     */
    bool forwardClientMsg(uint32_t clientConnId, uint8_t module, uint8_t sub,
                          const char* data, uint16_t len);

    /** @brief 发送内部协议消息 */
    bool sendMsg(uint16_t msgId, const char* data, uint16_t len);

private:
    uint32_t m_sceneServerId;              /**< ServerList server_id */
    std::unique_ptr<TcpClient> m_client;   /**< 出站 TcpClient */
};
