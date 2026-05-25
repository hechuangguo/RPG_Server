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

    virtual bool loadResources();
    bool start();
    void shutdown();

    bool addPlayer(UserID userId);
    bool removePlayer(UserID userId);
    bool hasPlayer(UserID userId) const;

    void setStartedCallback(SceneStartedCallback cb) { onStarted = std::move(cb); }
    void setStoppedCallback(SceneStoppedCallback cb) { onStopped = std::move(cb); }

    Scene(uint32_t sceneServerId, uint64_t sceneInstanceId, const MapConfig& cfg);

protected:
    virtual bool onLoadResources();
    virtual void onStartedHook();
    virtual void onShutdownHook();

    uint32_t            sceneServerId   = 0;
    uint64_t            sceneInstanceId = INVALID_SCENE_INSTANCE_ID;
    uint32_t            mapId           = 0;
    std::string         mapName;
    std::string         mapFile;
    uint32_t            maxPlayer       = 200;
    SceneState          state           = SceneState::CREATING;
    std::vector<UserID> players;
    SceneStartedCallback onStarted;
    SceneStoppedCallback onStopped;
};
