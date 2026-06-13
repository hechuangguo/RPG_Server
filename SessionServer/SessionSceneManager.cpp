/**
 * @file    SessionSceneManager.cpp
 * @brief  SessionSceneManager 实现
 */

#include "SessionSceneManager.h"
#include "../sdk/log/Logger.h"
#include <limits>

void SessionSceneManager::bindSceneServer(ConnID connId, uint32_t sceneServerId)
{
    auto it = m_sceneServers.find(sceneServerId);
    if (it != m_sceneServers.end())
    {
        it->second.connId = connId;
        it->second.alive  = true;
    }
    else
    {
        SessionSceneServerNode node;
        node.connId        = connId;
        node.sceneServerId = sceneServerId;
        node.alive         = true;
        m_sceneServers[sceneServerId] = node;
    }
    m_connToServerId[connId] = sceneServerId;
    LOG_INFO("SessionSceneManager bind SceneServer id=%u conn=%u", sceneServerId, connId);
}

void SessionSceneManager::unbindConn(ConnID connId)
{
    auto it = m_connToServerId.find(connId);
    if (it == m_connToServerId.end())
        return;

    const uint32_t serverId = it->second;
    m_sceneServers.erase(serverId);
    m_connToServerId.erase(it);

    for (auto sit = m_normalScenes.begin(); sit != m_normalScenes.end();)
    {
        if (sit->second.getSceneServerId() == serverId)
            sit = m_normalScenes.erase(sit);
        else
            ++sit;
    }
    for (auto cit = m_copyScenes.begin(); cit != m_copyScenes.end();)
    {
        if (cit->second.getSceneServerId() == serverId)
            cit = m_copyScenes.erase(cit);
        else
            ++cit;
    }

    LOG_WARN("SessionSceneManager unbind SceneServer id=%u conn=%u", serverId, connId);
}

ConnID SessionSceneManager::findConnBySceneServerId(uint32_t sceneServerId) const
{
    auto it = m_sceneServers.find(sceneServerId);
    if (it == m_sceneServers.end() || !it->second.alive)
        return INVALID_CONN_ID;
    return it->second.connId;
}

bool SessionSceneManager::registerScene(ConnID connId,
                                        const Msg_SES_SceneRegisterReq& req)
{
    bindSceneServer(connId, req.sceneServerId);

    SessionScene scene(req.sceneInstanceId, req.sceneServerId, req.mapId,
                       req.mapName, req.mapFile, req.maxPlayer);
    m_normalScenes[req.sceneInstanceId] = scene;
    adjustServerSceneCount(req.sceneServerId, 1);

    LOG_INFO("Session register scene instance=%llu map=%u server=%u kind=%u",
             req.sceneInstanceId, req.mapId, req.sceneServerId, req.sceneKind);
    return true;
}

bool SessionSceneManager::unregisterScene(uint64_t sceneInstanceId, uint32_t sceneServerId)
{
    if (m_normalScenes.erase(sceneInstanceId))
    {
        adjustServerSceneCount(sceneServerId, -1);
        return true;
    }
    if (m_copyScenes.erase(sceneInstanceId))
    {
        adjustServerSceneCount(sceneServerId, -1);
        return true;
    }
    return false;
}

SessionCopyScene* SessionSceneManager::findReusableCopy(CopyType type, uint32_t mapId,
                                                       uint64_t ownerId)
{
    for (auto& [id, copy] : m_copyScenes)
    {
        (void)id;
        if (copy.canReuse(type, mapId, ownerId))
            return &copy;
    }
    return nullptr;
}

SessionCopyScene* SessionSceneManager::createCopyRecord(uint32_t sceneServerId,
                                                        uint64_t copyInstanceId,
                                                        const Msg_SES_CopyCreateReq& req)
{
    SessionCopyScene copy(copyInstanceId, sceneServerId,
                          static_cast<CopyType>(req.copyType),
                          req.mapId, req.ownerId, req.mapName, req.mapFile, req.maxPlayer);
    m_copyScenes[copyInstanceId] = copy;
    adjustServerSceneCount(sceneServerId, 1);
    return &m_copyScenes[copyInstanceId];
}

SessionCopyScene* SessionSceneManager::findCopy(uint64_t copyInstanceId) const
{
    auto it = m_copyScenes.find(copyInstanceId);
    return it != m_copyScenes.end()
        ? const_cast<SessionCopyScene*>(&it->second) : nullptr;
}

SessionScene* SessionSceneManager::findNormalScene(uint64_t sceneInstanceId) const
{
    auto it = m_normalScenes.find(sceneInstanceId);
    return it != m_normalScenes.end()
        ? const_cast<SessionScene*>(&it->second) : nullptr;
}

uint32_t SessionSceneManager::resolveSceneServerByMapId(uint32_t mapId) const
{
    uint32_t bestServerId = 0;
    uint32_t bestPlayers  = std::numeric_limits<uint32_t>::max();

    for (const auto& [instanceId, scene] : m_normalScenes)
    {
        (void)instanceId;
        if (scene.getMapId() != mapId)
            continue;
        if (scene.getState() != SceneState::RUNNING)
            continue;

        const uint32_t serverId = scene.getSceneServerId();
        auto sit = m_sceneServers.find(serverId);
        if (sit == m_sceneServers.end() || !sit->second.alive)
            continue;

        const uint32_t players = scene.getPlayerCount();
        if (players < bestPlayers)
        {
            bestPlayers  = players;
            bestServerId = serverId;
        }
    }
    return bestServerId;
}

uint32_t SessionSceneManager::pickSceneServerId() const
{
    uint32_t bestId   = 0;
    uint32_t bestLoad = std::numeric_limits<uint32_t>::max();

    for (const auto& [id, node] : m_sceneServers)
    {
        (void)id;
        if (!node.alive || node.connId == INVALID_CONN_ID)
            continue;
        const uint32_t load = node.sceneCount * 10 + node.playerCount;
        if (load < bestLoad)
        {
            bestLoad = load;
            bestId   = node.sceneServerId;
        }
    }
    return bestId;
}

uint64_t SessionSceneManager::generateCopyInstanceId()
{
    return m_nextCopyInstanceId++;
}

void SessionSceneManager::adjustServerSceneCount(uint32_t sceneServerId, int delta)
{
    auto it = m_sceneServers.find(sceneServerId);
    if (it == m_sceneServers.end())
        return;
    if (delta > 0)
        it->second.sceneCount += static_cast<uint32_t>(delta);
    else if (it->second.sceneCount >= static_cast<uint32_t>(-delta))
        it->second.sceneCount -= static_cast<uint32_t>(-delta);
    else
        it->second.sceneCount = 0;
}
