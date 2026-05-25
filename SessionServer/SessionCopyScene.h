/**
 * @file    SessionCopyScene.h
 * @brief  SessionServer 侧副本场景注册信息
 */

#pragma once
#include "../protocal/InternalMsg.h"
#include <cstdint>
#include <string>

/**
 * @brief 单个副本实例在 Session 上的注册记录
 */
class SessionCopyScene
{
public:
    SessionCopyScene() = default;

    SessionCopyScene(uint64_t instanceId, uint32_t serverId, CopyType type,
                     uint32_t mapId, uint64_t owner, const std::string& name,
                     const std::string& file, uint32_t maxPlayer)
        : copyInstanceId(instanceId)
        , sceneServerId(serverId)
        , copyType(type)
        , mapId(mapId)
        , ownerId(owner)
        , mapName(name)
        , mapFile(file)
        , maxPlayer(maxPlayer)
        , state(SceneState::RUNNING)
    {
    }

    uint64_t   getCopyInstanceId() const { return copyInstanceId; }
    uint32_t   getSceneServerId() const { return sceneServerId; }
    CopyType   getCopyType() const { return copyType; }
    uint32_t   getMapId() const { return mapId; }
    uint64_t   getOwnerId() const { return ownerId; }
    uint32_t   getMaxPlayer() const { return maxPlayer; }
    uint32_t   getPlayerCount() const { return playerCount; }
    SceneState getState() const { return state; }

    bool isFull() const { return playerCount >= maxPlayer; }
    bool canReuse(CopyType type, uint32_t map, uint64_t owner) const
    {
        return state == SceneState::RUNNING
            && copyType == type
            && mapId == map
            && ownerId == owner
            && !isFull();
    }

    void setPlayerCount(uint32_t n) { playerCount = n; }
    void setState(SceneState s) { state = s; }

private:
    uint64_t   copyInstanceId = INVALID_SCENE_INSTANCE_ID;
    uint32_t   sceneServerId  = 0;
    CopyType   copyType       = CopyType::TEAM;
    uint32_t   mapId          = 0;
    uint64_t   ownerId        = 0;
    std::string mapName;
    std::string mapFile;
    uint32_t   maxPlayer      = 5;
    uint32_t   playerCount    = 0;
    SceneState state          = SceneState::CREATING;
};
