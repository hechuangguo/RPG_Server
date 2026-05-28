/**
 * @file    Scene.h
 * @brief  场景基类 —— 普通地图与副本的公共逻辑
 */

#pragma once
#include "../protocal/InternalMsg.h"
#include "../sdk/util/SceneInfoLoader.h"
#include "../sdk/util/UserBase.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

/** @brief 生成普通场景实例 ID（定义于 InternalMsg.h，此处 using 便于 Scene 模块引用） */
using SceneStartedCallback = std::function<void(class Scene&)>;

/** @brief 场景关闭时的回调 */
using SceneStoppedCallback = std::function<void(class Scene&)>;

/**
 * @brief 场景基类
 *
 * 普通地图与副本共用：加载资源、玩家列表、状态机。
 */
class Scene
{
public:
    virtual ~Scene() = default;

    virtual SceneKind getSceneKind() const { return SceneKind::NORMAL; }

    uint64_t getSceneInstanceId() const { return sceneInstanceId; }
    uint32_t getMapId() const { return mapId; }
    uint32_t getSceneServerId() const { return sceneServerId; }
    const std::string& getMapName() const { return mapName; }
    const std::string& getMapFile() const { return mapFile; }
    uint32_t getMaxPlayer() const { return maxPlayer; }
    SceneState getState() const { return state; }
    size_t getPlayerCount() const { return players.size(); }

    /** @brief 加载场景资源（模板方法，内部会调用 onLoadResources） */
    virtual bool loadResources();
    /** @brief 启动场景并切换到 RUNNING */
    bool start();
    /** @brief 关闭场景并触发停止回调 */
    void shutdown();

    /** @brief 添加玩家到场景（已存在则返回 false） */
    bool addPlayer(UserID userId);
    /** @brief 从场景移除玩家 */
    bool removePlayer(UserID userId);
    /** @brief 查询玩家是否在场景内 */
    bool hasPlayer(UserID userId) const;

    void setStartedCallback(SceneStartedCallback cb) { onStarted = std::move(cb); }
    void setStoppedCallback(SceneStoppedCallback cb) { onStopped = std::move(cb); }

    /** @brief 用地图配置构造场景基础信息 */
    Scene(uint32_t sceneServerId, uint64_t sceneInstanceId, const MapConfig& cfg);

protected:
    /** @brief 子类资源加载扩展点 */
    virtual bool onLoadResources();
    /** @brief 子类启动后钩子 */
    virtual void onStartedHook();
    /** @brief 子类关闭前钩子 */
    virtual void onShutdownHook();

    uint32_t            sceneServerId   = 0;                       /**< 承载该场景的 SceneServer ID */
    uint64_t            sceneInstanceId = INVALID_SCENE_INSTANCE_ID; /**< 场景实例 ID */
    uint32_t            mapId           = 0;                       /**< 地图模板 ID */
    std::string         mapName;                                   /**< 地图名称 */
    std::string         mapFile;                                   /**< 地图文件路径 */
    uint32_t            maxPlayer       = 200;                     /**< 最大容纳玩家数 */
    SceneState          state           = SceneState::CREATING;    /**< 生命周期状态 */
    std::vector<UserID> players;                                   /**< 当前玩家 ID 列表 */
    SceneStartedCallback onStarted;                                /**< 场景启动回调 */
    SceneStoppedCallback onStopped;                                /**< 场景关闭回调 */
};
