/**
 * @file    GatewayScenePool.cpp
 * @brief   GatewayScenePool 实现
 */

#include "GatewayScenePool.h"

#include "../sdk/log/Logger.h"

bool GatewayScenePool::connectAll(const ServerList& list)
{
    m_links.clear();
    std::vector<const ServerEntry*> scenes;
    list.findAll(SubServerType::SCENE, scenes);
    if (scenes.empty())
    {
        LOG_WARN("GatewayScenePool: no SCENE entries in ServerList");
        return false;
    }

    for (const ServerEntry* entry : scenes)
    {
        SceneLink link;
        link.serverId = entry->id;
        link.client = std::make_unique<TcpClient>(m_cb);
        if (!link.client->Connect(entry->ip, entry->port))
        {
            LOG_WARN("GatewayScenePool: connect failed sceneId=%u %s:%u",
                     entry->id, entry->ip.c_str(), entry->port);
            continue;
        }
        LOG_INFO("GatewayScenePool: connecting sceneId=%u %s:%u",
                 entry->id, entry->ip.c_str(), entry->port);
        m_links.push_back(std::move(link));
    }
    return !m_links.empty();
}

void GatewayScenePool::pollAll()
{
    for (auto& link : m_links)
    {
        if (link.client)
            link.client->Poll(0);
    }
}

TcpClient* GatewayScenePool::clientFor(uint32_t sceneServerId)
{
    for (auto& link : m_links)
    {
        if (link.serverId == sceneServerId && link.client && link.client->IsConnected())
            return link.client.get();
    }
    return nullptr;
}

TcpClient* GatewayScenePool::firstConnected()
{
    for (auto& link : m_links)
    {
        if (link.client && link.client->IsConnected())
            return link.client.get();
    }
    return nullptr;
}

bool GatewayScenePool::hasAnyConnected() const
{
    for (const auto& link : m_links)
    {
        if (link.client && link.client->IsConnected())
            return true;
    }
    return false;
}
