/**
 * @file    SceneManager.h
 * @brief  管理 SceneServer 进程内所有场景（普通 + 副本）
 */

#pragma once
#include "Scene.h"
#include "CopyScene.h"
#include "../sdk/util/SceneInfoLoader.h"
#include "../sdk/util/Singleton.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>

/**
 * @brief SceneServer 进程内场景管理器（单例）
 */
class SceneManager : public LazySingleton<SceneManager>
{
public:
    friend class LazySingleton<SceneManager>;

    /** @brief 获取全局唯一实例 */
    static SceneManager& Instance() { return LazySingleton<SceneManager>::Instance(); }

    /** @brief 按配置创建本 SceneServer 负责的普通场景 */
    bool createNormalScenesFromConfig(uint32_t sceneServerId,
                                      const SceneServerInfo& info);

    /** @brief 创建副本场景并加入管理表 */
    std::shared_ptr<CopyScene> createCopyScene(uint32_t sceneServerId,
                                               const CopySceneDef& def);

    /** @brief 按场景实例 ID 查找场景 */
    std::shared_ptr<Scene> findScene(uint64_t sceneInstanceId) const;

    /** @brief 按地图 ID 查找普通场景 */
    std::shared_ptr<Scene> findNormalSceneByMapId(uint32_t mapId) const;

    /** @brief 按副本实例 ID 查找副本场景 */
    std::shared_ptr<CopyScene> findCopyScene(uint64_t copyInstanceId) const;

    /** @brief 移除指定场景并触发关闭回调 */
    bool removeScene(uint64_t sceneInstanceId);

    /** @brief 当前管理的场景总数 */
    size_t getSceneCount() const { return m_scenes.size(); }

    /** @brief 遍历所有场景执行只读访问 */
    void forEach(const std::function<void(const std::shared_ptr<Scene>&)>& fn) const;

    /** @brief 设置场景启动回调 */
    void setStartedCallback(SceneStartedCallback cb) { m_onStarted = std::move(cb); }

    /** @brief 设置场景关闭回调 */
    void setStoppedCallback(SceneStoppedCallback cb) { m_onStopped = std::move(cb); }

private:
    SceneManager() = default;

    /** @brief 将场景加入管理表并更新辅助索引 */
    bool addScene(std::shared_ptr<Scene> scene);
    std::unordered_map<uint64_t, std::shared_ptr<Scene>> m_scenes;          /**< sceneInstanceId -> Scene */
    std::unordered_map<uint32_t, uint64_t>               m_mapIdToInstance; /**< mapId -> sceneInstanceId（普通图） */
    SceneStartedCallback m_onStarted;                                        /**< 场景启动回调 */
    SceneStoppedCallback m_onStopped;                                        /**< 场景关闭回调 */
};
