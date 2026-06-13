/**
 * @file    Scene.cpp
 * @brief  Scene 基类实现
 */

#include "Scene.h"
#include "../sdk/log/Logger.h"
#include <algorithm>

Scene::Scene(uint32_t serverId, uint64_t instanceId, const MapConfig& cfg)
    : sceneServerId(serverId)
    , sceneInstanceId(instanceId)
    , mapId(cfg.mapID)
    , mapName(cfg.mapName)
    , mapFile(cfg.mapFile)
    , maxPlayer(cfg.maxPlayer)
{
}

bool Scene::loadResources()
{
    state = SceneState::CREATING;
    LOG_INFO("Scene loadResources: instance=%llu map=%u file=%s",
             sceneInstanceId, mapId, mapFile.c_str());

    if (!onLoadResources())
    {
        LOG_ERR("Scene loadResources failed: instance=%llu map=%u",
                sceneInstanceId, mapId);
        return false;
    }

    LOG_DEBUG("Scene resources loaded: map=%u (%s)", mapId, mapName.c_str());
    return true;
}

bool Scene::start()
{
    if (state == SceneState::RUNNING)
        return true;

    if (!loadResources())
        return false;

    state = SceneState::RUNNING;
    onStartedHook();

    if (onStarted)
        onStarted(*this);

    LOG_INFO("Scene started: instance=%llu map=%u kind=%u",
             sceneInstanceId, mapId, static_cast<unsigned>(getSceneKind()));
    return true;
}

void Scene::shutdown()
{
    if (state == SceneState::CLOSED || state == SceneState::CLOSING)
        return;

    state = SceneState::CLOSING;
    onShutdownHook();

    if (onStopped)
        onStopped(*this);

    players.clear();
    state = SceneState::CLOSED;
    LOG_INFO("Scene shutdown: instance=%llu map=%u", sceneInstanceId, mapId);
}

bool Scene::onLoadResources()
{
    if (mapFile.empty())
        LOG_WARN("Scene mapFile empty, map=%u", mapId);
    return true;
}

void Scene::onStartedHook()
{
}

void Scene::onShutdownHook()
{
}

bool Scene::addPlayer(UserID userId)
{
    if (hasPlayer(userId))
        return false;
    if (players.size() >= maxPlayer)
        return false;
    players.push_back(userId);
    return true;
}

bool Scene::removePlayer(UserID userId)
{
    auto it = std::find(players.begin(), players.end(), userId);
    if (it == players.end())
        return false;
    players.erase(it);
    return true;
}

bool Scene::hasPlayer(UserID userId) const
{
    return std::find(players.begin(), players.end(), userId) != players.end();
}
