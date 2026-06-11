/**
 * @file    GatewayScenePool.h
 * @brief   Gateway 到多个 SceneServer 实例的出站连接池
 *
 * 按 ServerList 中全部 SCENE 条目各建一条 TcpClient；上行按用户绑定的 sceneServerId 选路。
 */

#pragma once

#include "../sdk/net/TcpClient.h"
#include "../sdk/util/ServerList.h"
#include "../protocal/InternalMsg.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

/**
 * @brief 管理 Gateway → 多 SceneServer 的 TcpClient 连接
 */
class GatewayScenePool
{
public:
    /**
     * @brief 构造连接池
     * @param cb 出站连接回调（通常为 GatewayUpstreamCallback）
     */
    explicit GatewayScenePool(INetCallback* cb)
        : m_cb(cb)
    {}

    /**
     * @brief 按 ServerList 连接全部 SCENE 实例
     * @param list 集群拓扑
     * @return 至少成功发起一条连接返回 true（无 SCENE 条目时 false）
     */
    bool connectAll(const ServerList& list);

    /** @brief 轮询所有 Scene 出站连接 */
    void pollAll();

    /**
     * @brief 按 sceneServerId 查找连接
     * @param sceneServerId ServerList 中的 server_id
     * @return 对应 TcpClient；未找到时 nullptr
     */
    TcpClient* clientFor(uint32_t sceneServerId);

    /** @brief 任意一条已连接 Scene（兜底路由） */
    TcpClient* firstConnected();

    /** @brief 是否至少有一条 Scene 连接存活 */
    bool hasAnyConnected() const;

private:
    INetCallback* m_cb; /**< 出站回调（非拥有） */
    struct SceneLink
    {
        uint32_t serverId = 0;
        std::unique_ptr<TcpClient> client;
    };
    std::vector<SceneLink> m_links; /**< 全部 Scene 出站连接 */
};
