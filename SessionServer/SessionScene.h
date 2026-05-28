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
    SessionScene() = default;

    SessionScene(uint64_t instanceId, uint32_t serverId, uint32_t mapId,
                 const std::string& name, const std::string& file, uint32_t maxPlayer)
        : sceneInstanceId(instanceId)
        , sceneServerId(serverId)
        , mapId(mapId)
        , mapName(name)
        , mapFile(file)
        , maxPlayer(maxPlayer)
        , state(SceneState::RUNNING)
    {
    }

    uint64_t    getSceneInstanceId() const { return sceneInstanceId; }
    uint32_t    getSceneServerId() const { return sceneServerId; }
    uint32_t    getMapId() const { return mapId; }
    const std::string& getMapName() const { return mapName; }
    uint32_t    getMaxPlayer() const { return maxPlayer; }
    uint32_t    getPlayerCount() const { return playerCount; }
    SceneState  getState() const { return state; }

    void setPlayerCount(uint32_t n) { playerCount = n; }
    void setState(SceneState s) { state = s; }

private:
    uint64_t    sceneInstanceId = INVALID_SCENE_INSTANCE_ID; /**< 场景实例 ID */
    uint32_t    sceneServerId   = 0;                         /**< 承载该场景的 SceneServer ID */
    uint32_t    mapId           = 0;                         /**< 地图模板 ID */
    std::string mapName;                                     /**< 地图显示名 */
    std::string mapFile;                                     /**< 地图资源文件 */
    uint32_t    maxPlayer       = 200;                       /**< 场景人数上限 */
    uint32_t    playerCount     = 0;                         /**< 当前在线人数 */
    SceneState  state           = SceneState::CREATING;      /**< 场景生命周期状态 */
};
