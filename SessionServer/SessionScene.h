/**
 * @file    SessionScene.h
 * @brief  SessionServer 侧普通场景注册信息
 */

#pragma once
#include "../protocal/InternalMsg.h"
#include <cstdint>
#include <string>

/**
 * @brief 单个普通地图场景在 Session 上的注册记录
 */
class SessionScene
{
public:
    /** @brief 默认构造（未注册状态） */
    SessionScene() = default;

    /** @brief 构造已注册普通场景记录 */
    SessionScene(uint64_t instanceId, uint32_t serverId, uint32_t mapId,
                 const std::string& name, uint32_t maxPlayer)
        : sceneInstanceId(instanceId)
        , sceneServerId(serverId)
        , mapId(mapId)
        , mapName(name)
        , maxPlayer(maxPlayer)
        , state(SceneState::RUNNING)
    {
    }

    /** @brief 场景实例 ID */
    uint64_t    getSceneInstanceId() const { return sceneInstanceId; }

    /** @brief 承载该场景的 SceneServer ID */
    uint32_t    getSceneServerId() const { return sceneServerId; }

    /** @brief 地图模板 ID */
    uint32_t    getMapId() const { return mapId; }

    /** @brief 地图显示名 */
    const std::string& getMapName() const { return mapName; }

    /** @brief 场景人数上限 */
    uint32_t    getMaxPlayer() const { return maxPlayer; }

    /** @brief 当前在线人数 */
    uint32_t    getPlayerCount() const { return playerCount; }

    /** @brief 场景生命周期状态 */
    SceneState  getState() const { return state; }

    /** @brief 更新当前在线人数 */
    void setPlayerCount(uint32_t n) { playerCount = n; }

    /** @brief 更新场景状态 */
    void setState(SceneState s) { state = s; }

private:
    uint64_t    sceneInstanceId = INVALID_SCENE_INSTANCE_ID; /**< 场景实例 ID */
    uint32_t    sceneServerId   = 0;                         /**< 承载该场景的 SceneServer ID */
    uint32_t    mapId           = 0;                         /**< 地图模板 ID */
    std::string mapName;                                     /**< 地图显示名 */
    uint32_t    maxPlayer       = 200;                       /**< 场景人数上限 */
    uint32_t    playerCount     = 0;                         /**< 当前在线人数 */
    SceneState  state           = SceneState::CREATING;      /**< 场景生命周期状态 */
};
