/**
 * @file    Scene.h
 * @brief  场景基类 —— 普通地图与副本的公共逻辑
 */

#pragma once
#include "../protocal/InternalMsg.h"
#include "../sdk/util/SceneInfoLoader.h"
#include "../sdk/util/MapRuntimeTypes.h"
#include "../sdk/util/UserBase.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

/** @brief 场景启动完成时的回调 */
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

    /** @brief 场景类型（普通 / 副本） */
    virtual SceneKind getSceneKind() const { return SceneKind::NORMAL; }

    /** @brief 场景实例 ID */
    uint64_t getSceneInstanceId() const { return sceneInstanceId; }

    /** @brief 地图模板 ID */
    uint32_t getMapId() const { return mapId; }

    /** @brief 承载该场景的 SceneServer ID */
    uint32_t getSceneServerId() const { return sceneServerId; }

    /** @brief 地图显示名 */
    const std::string& getMapName() const { return mapName; }

    /** @brief 最大容纳玩家数 */
    uint32_t getMaxPlayer() const { return maxPlayer; }

    /** @brief 地图几何数据（Common/map/） */
    std::shared_ptr<MapRuntimeData> getMapData() const { return mapRuntimeData; }

    /** @brief AOI 格子边长；0 表示使用 AOIServer 全局默认 */
    float getAoiGridSize() const;

    /** @brief 场景生命周期状态 */
    SceneState getState() const { return state; }

    /** @brief 当前场景内玩家数量 */
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

    /** @brief 注册场景启动回调 */
    void setStartedCallback(SceneStartedCallback cb) { onStarted = std::move(cb); }

    /** @brief 注册场景关闭回调 */
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
    uint32_t            maxPlayer       = 200;                     /**< 最大容纳玩家数 */
    uint32_t            expectedVersion = 0;                     /**< 策划表 version，用于几何校验 */
    std::shared_ptr<MapRuntimeData> mapRuntimeData;              /**< 几何 runtime 数据 */
    SceneState          state           = SceneState::CREATING;    /**< 生命周期状态 */
    std::vector<UserID> players;                                   /**< 当前玩家 ID 列表 */
    SceneStartedCallback onStarted;                                /**< 场景启动回调 */
    SceneStoppedCallback onStopped;                                /**< 场景关闭回调 */
};
