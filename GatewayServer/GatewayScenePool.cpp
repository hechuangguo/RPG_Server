/**
 * @file    GatewayScenePool.cpp
 * @brief   GatewayScenePool 实现
 */

#include "GatewayScenePool.h"

#include "../sdk/log/Logger.h"

bool GatewayScenePool::connectAll(const ServerList& list)
{
    m_clients.clear();
    std::vector<const ServerEntry*> scenes;
    list.findAll(SubServerType::SCENE, scenes);
    if (scenes.empty())
    {
        LOG_WARN("场景连接池: 服务器列表中没有场景服条目");
        return false;
    }

    for (const ServerEntry* entry : scenes)
    {
        auto link = std::make_unique<SceneClient>(entry->id, m_cb);
        if (!link->connect(entry->ip, entry->port))
            continue;
        m_clients.push_back(std::move(link));
    }
    return !m_clients.empty();
}

void GatewayScenePool::pollAll()
{
    for (auto& client : m_clients)
    {
        if (client)
            client->poll();
    }
}

SceneClient* GatewayScenePool::clientFor(uint32_t sceneServerId)
{
    for (auto& client : m_clients)
    {
        if (client && client->sceneServerId() == sceneServerId && client->isConnected())
            return client.get();
    }
    return nullptr;
}

TcpClient* GatewayScenePool::clientTcpFor(uint32_t sceneServerId)
{
    SceneClient* sc = clientFor(sceneServerId);
    return sc ? sc->tcpClient() : nullptr;
}

SceneClient* GatewayScenePool::firstConnected()
{
    for (auto& client : m_clients)
    {
        if (client && client->isConnected())
            return client.get();
    }
    return nullptr;
}

bool GatewayScenePool::hasAnyConnected() const
{
    for (const auto& client : m_clients)
    {
        if (client && client->isConnected())
            return true;
    }
    return false;
}
