/**
 * @file    SceneManager.cpp
 * @brief  SceneManager 实现
 */

#include "SceneManager.h"
#include "../sdk/util/MapConfigLoader.h"
#include "../sdk/log/Logger.h"

#include <sys/stat.h>

namespace
{

bool commonMapDirExists(uint32_t mapId)
{
    const std::string path = "Common/map/" + std::to_string(mapId);
    struct stat st{};
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

MapConfig mergeMapConfig(const MapConfig& xmlCfg)
{
    MapConfig out = xmlCfg;
    const MapTableEntry* entry = MapConfigLoader::find(xmlCfg.mapID);
    if (!entry)
        return out;

    if (out.mapName.empty())
        out.mapName = entry->name;
    if (out.maxPlayer == 0)
        out.maxPlayer = entry->maxPlayer;
    out.expectedVersion = entry->version;
    return out;
}

} // namespace

bool SceneManager::createNormalScenesFromConfig(uint32_t sceneServerId,
                                                const SceneServerInfo& info)
{
    std::string err;
    if (!MapConfigLoader::load(&err))
        LOG_WARN("策划地图表加载失败，将仅使用 server_info: %s", err.c_str());

    bool allOk = true;
    for (const auto& mc : info.maps)
    {
        const MapTableEntry* entry = MapConfigLoader::find(mc.mapID);
        if (MapConfigLoader::isLoaded())
        {
            if (!entry)
            {
                LOG_WARN("策划地图表无 map=%u，跳过场景创建", mc.mapID);
                allOk = false;
                continue;
            }
            if (!entry->enabled)
            {
                LOG_WARN("地图未开放 map=%u，跳过场景创建", mc.mapID);
                continue;
            }
        }

        if (!commonMapDirExists(mc.mapID))
        {
            LOG_WARN("Common/map 目录不存在 map=%u，跳过场景创建", mc.mapID);
            allOk = false;
            continue;
        }

        MapConfig resolved = mergeMapConfig(mc);
        if (resolved.mapName.empty())
            resolved.mapName = "map_" + std::to_string(resolved.mapID);
        if (resolved.maxPlayer == 0)
            resolved.maxPlayer = 200;

        const uint64_t instanceId = makeNormalSceneInstanceId(sceneServerId, resolved.mapID);
        auto scene = std::make_shared<Scene>(sceneServerId, instanceId, resolved);
        scene->setStartedCallback(m_onStarted);
        scene->setStoppedCallback(m_onStopped);

        if (!scene->start())
        {
            LOG_ERR("普通场景启动失败: map=%u", resolved.mapID);
            allOk = false;
            continue;
        }

        if (!addScene(scene))
        {
            LOG_ERR("普通场景重复注册: map=%u", resolved.mapID);
            allOk = false;
        }
    }
    return allOk;
}

std::shared_ptr<CopyScene> SceneManager::createCopyScene(uint32_t sceneServerId,
                                                         const CopySceneDef& def)
{
    if (def.copyInstanceId == INVALID_SCENE_INSTANCE_ID)
        return nullptr;
    if (m_scenes.count(def.copyInstanceId))
        return findCopyScene(def.copyInstanceId);

    auto copy = CopySceneFactory::create(sceneServerId, def);
    copy->setStartedCallback(m_onStarted);
    copy->setStoppedCallback(m_onStopped);

    if (!copy->start())
    {
        LOG_ERR("副本场景启动失败: instance=%llu", def.copyInstanceId);
        return nullptr;
    }

    if (!addScene(copy))
        return nullptr;

    return copy;
}

std::shared_ptr<Scene> SceneManager::findScene(uint64_t sceneInstanceId) const
{
    auto it = m_scenes.find(sceneInstanceId);
    return it != m_scenes.end() ? it->second : nullptr;
}

std::shared_ptr<Scene> SceneManager::findNormalSceneByMapId(uint32_t mapId) const
{
    auto it = m_mapIdToInstance.find(mapId);
    if (it == m_mapIdToInstance.end())
        return nullptr;
    auto scene = findScene(it->second);
    if (scene && scene->getSceneKind() == SceneKind::NORMAL)
        return scene;
    return nullptr;
}

std::shared_ptr<CopyScene> SceneManager::findCopyScene(uint64_t copyInstanceId) const
{
    auto scene = findScene(copyInstanceId);
    if (!scene || scene->getSceneKind() != SceneKind::COPY)
        return nullptr;
    return std::static_pointer_cast<CopyScene>(scene);
}

bool SceneManager::removeScene(uint64_t sceneInstanceId)
{
    auto it = m_scenes.find(sceneInstanceId);
    if (it == m_scenes.end())
        return false;

    if (it->second && it->second->getSceneKind() == SceneKind::NORMAL)
        m_mapIdToInstance.erase(it->second->getMapId());

    it->second->shutdown();
    m_scenes.erase(it);
    return true;
}

bool SceneManager::addScene(std::shared_ptr<Scene> scene)
{
    if (!scene)
        return false;

    const uint64_t instanceId = scene->getSceneInstanceId();
    if (m_scenes.count(instanceId))
        return false;

    if (scene->getSceneKind() == SceneKind::NORMAL)
    {
        const uint32_t mapId = scene->getMapId();
        if (m_mapIdToInstance.count(mapId))
            return false;
        m_mapIdToInstance[mapId] = instanceId;
    }

    m_scenes[instanceId] = std::move(scene);
    return true;
}

void SceneManager::forEach(const std::function<void(const std::shared_ptr<Scene>&)>& fn) const
{
    for (const auto& [id, scene] : m_scenes)
    {
        (void)id;
        if (scene)
            fn(scene);
    }
}
