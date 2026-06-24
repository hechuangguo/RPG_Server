/**
 * @file    Scene.cpp
 * @brief  Scene 基类实现
 */

#include "Scene.h"
#include "MapDataLoader.h"
#include "../sdk/log/Logger.h"
#include <algorithm>

Scene::Scene(uint32_t serverId, uint64_t instanceId, const MapConfig& cfg)
    : sceneServerId(serverId)
    , sceneInstanceId(instanceId)
    , mapId(cfg.mapID)
    , mapName(cfg.mapName)
    , maxPlayer(cfg.maxPlayer > 0 ? cfg.maxPlayer : 200)
    , expectedVersion(cfg.expectedVersion)
{
}

bool Scene::loadResources()
{
    state = SceneState::CREATING;
    LOG_INFO("场景加载资源: instance=%llu map=%u",
             sceneInstanceId, mapId);

    if (!onLoadResources())
    {
        LOG_ERR("场景加载资源失败: instance=%llu map=%u",
                sceneInstanceId, mapId);
        return false;
    }

    LOG_DEBUG("场景资源加载完成: map=%u (%s)", mapId, mapName.c_str());
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

    LOG_INFO("场景启动完成: instance=%llu map=%u kind=%u",
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
    LOG_INFO("场景已关闭: instance=%llu map=%u", sceneInstanceId, mapId);
}

bool Scene::onLoadResources()
{
    mapRuntimeData = loadMapData(mapId, expectedVersion);
    if (!mapRuntimeData)
    {
        LOG_ERR("场景地图几何加载失败: map=%u", mapId);
        return false;
    }
    return true;
}

float Scene::getAoiGridSize() const
{
    if (mapRuntimeData && mapRuntimeData->aoiGridSize > 0.f)
        return mapRuntimeData->aoiGridSize;
    return 0.f;
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
