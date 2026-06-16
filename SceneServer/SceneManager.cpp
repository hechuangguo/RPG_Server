/**
 * @file    SceneManager.cpp
 * @brief  SceneManager 实现
 */

#include "SceneManager.h"
#include "../sdk/log/Logger.h"

bool SceneManager::createNormalScenesFromConfig(uint32_t sceneServerId,
                                                const SceneServerInfo& info)
{
    bool allOk = true;
    for (const auto& mc : info.maps)
    {
        const uint64_t instanceId = makeNormalSceneInstanceId(sceneServerId, mc.mapID);
        auto scene = std::make_shared<Scene>(sceneServerId, instanceId, mc);
        scene->setStartedCallback(m_onStarted);
        scene->setStoppedCallback(m_onStopped);

        if (!scene->start())
        {
            LOG_ERR("普通场景启动失败: map=%u", mc.mapID);
            allOk = false;
            continue;
        }

        if (!addScene(scene))
        {
            LOG_ERR("普通场景重复注册: map=%u", mc.mapID);
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
    auto scene = findScene(sceneInstanceId);
    if (!scene)
        return false;

    scene->shutdown();
    if (scene->getSceneKind() == SceneKind::NORMAL)
        m_mapIdToInstance.erase(scene->getMapId());
    m_scenes.erase(sceneInstanceId);
    return true;
}

void SceneManager::forEach(const std::function<void(const std::shared_ptr<Scene>&)>& fn) const
{
    for (const auto& [id, scene] : m_scenes)
    {
        (void)id;
        fn(scene);
    }
}

bool SceneManager::addScene(std::shared_ptr<Scene> scene)
{
    if (!scene)
        return false;

    const uint64_t id = scene->getSceneInstanceId();
    if (m_scenes.count(id))
        return false;

    m_scenes[id] = scene;
    if (scene->getSceneKind() == SceneKind::NORMAL)
        m_mapIdToInstance[scene->getMapId()] = id;
    return true;
}
