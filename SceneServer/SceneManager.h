/**
 * @file    SceneManager.h
 * @brief  管理 SceneServer 进程内所有场景（普通 + 副本）
 */

#pragma once
#include "Scene.h"
#include "CopyScene.h"
#include "../sdk/util/SceneInfoLoader.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>

/**
 * @brief SceneServer 进程内场景管理器
 */
class SceneManager
{
public:
    bool createNormalScenesFromConfig(uint32_t sceneServerId,
                                      const SceneServerInfo& info);

    std::shared_ptr<CopyScene> createCopyScene(uint32_t sceneServerId,
                                               const CopySceneDef& def);

    std::shared_ptr<Scene> findScene(uint64_t sceneInstanceId) const;
    std::shared_ptr<Scene> findNormalSceneByMapId(uint32_t mapId) const;
    std::shared_ptr<CopyScene> findCopyScene(uint64_t copyInstanceId) const;

    bool removeScene(uint64_t sceneInstanceId);

    size_t getSceneCount() const { return m_scenes.size(); }

    void forEach(const std::function<void(const std::shared_ptr<Scene>&)>& fn) const;

    void setStartedCallback(SceneStartedCallback cb) { m_onStarted = std::move(cb); }
    void setStoppedCallback(SceneStoppedCallback cb) { m_onStopped = std::move(cb); }

private:
    bool addScene(std::shared_ptr<Scene> scene);

    std::unordered_map<uint64_t, std::shared_ptr<Scene>> m_scenes;
    std::unordered_map<uint32_t, uint64_t>               m_mapIdToInstance;
    SceneStartedCallback m_onStarted;
    SceneStoppedCallback m_onStopped;
};
