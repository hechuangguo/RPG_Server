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
    uint64_t   copyInstanceId = INVALID_SCENE_INSTANCE_ID; /**< 副本实例 ID */
    uint32_t   sceneServerId  = 0;                         /**< 承载该副本的 SceneServer ID */
    CopyType   copyType       = CopyType::TEAM;            /**< 副本类型（队伍/单人/公会） */
    uint32_t   mapId          = 0;                         /**< 副本地图 ID */
    uint64_t   ownerId        = 0;                         /**< 副本归属者（队长/玩家/公会） */
    std::string mapName;                                   /**< 地图名称 */
    std::string mapFile;                                   /**< 地图资源文件 */
    uint32_t   maxPlayer      = 5;                         /**< 人数上限 */
    uint32_t   playerCount    = 0;                         /**< 当前人数 */
    SceneState state          = SceneState::CREATING;      /**< 副本状态机 */
};
