/**
 * @file    GatewayScenePool.h
 * @brief   Gateway 到多个 SceneServer 实例的出站连接池
 *
 * 按 ServerList 中全部 SCENE 条目各建一条 SceneClient；上行按用户绑定的 sceneServerId 选路。
 */

#pragma once

#include "SceneClient.h"
#include "../sdk/net/TcpClient.h"
#include "../sdk/util/ServerList.h"

#include <cstdint>
#include <memory>
#include <vector>

/**
 * @brief 管理 Gateway → 多 SceneServer 的 SceneClient 连接池
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
     * @brief 按 sceneServerId 查找 SceneClient
     * @param sceneServerId ServerList 中的 server_id
     * @return 对应 SceneClient；未找到时 nullptr
     */
    SceneClient* clientFor(uint32_t sceneServerId);

    /**
     * @brief 按 sceneServerId 查找底层 TcpClient（兼容旧 API）
     * @deprecated 优先使用 SceneClient::forwardClientMsg
     */
    TcpClient* clientTcpFor(uint32_t sceneServerId);

    /** @deprecated 不再用于业务路由兜底；仅测试/诊断 */
    SceneClient* firstConnected();

    /** @brief 是否至少有一条 Scene 连接存活 */
    bool hasAnyConnected() const;

private:
    INetCallback* m_cb;                         /**< 出站回调（非拥有） */
    std::vector<std::unique_ptr<SceneClient>> m_clients; /**< 全部 Scene 出站连接 */
};
