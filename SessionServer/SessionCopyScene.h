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
    /** @brief 默认构造（未注册状态） */
    SessionCopyScene() = default;

    /** @brief 构造已注册副本记录 */
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

    /** @brief 副本实例 ID */
    uint64_t   getCopyInstanceId() const { return copyInstanceId; }

    /** @brief 承载该副本的 SceneServer ID */
    uint32_t   getSceneServerId() const { return sceneServerId; }

    /** @brief 副本类型 */
    CopyType   getCopyType() const { return copyType; }

    /** @brief 副本地图 ID */
    uint32_t   getMapId() const { return mapId; }

    /** @brief 副本归属者 ID */
    uint64_t   getOwnerId() const { return ownerId; }

    /** @brief 副本人数上限 */
    uint32_t   getMaxPlayer() const { return maxPlayer; }

    /** @brief 当前副本人数 */
    uint32_t   getPlayerCount() const { return playerCount; }

    /** @brief 副本生命周期状态 */
    SceneState getState() const { return state; }

    /** @brief 是否已达人数上限 */
    bool isFull() const { return playerCount >= maxPlayer; }

    /** @brief 是否可复用已有副本（类型/地图/归属一致且未满） */
    bool canReuse(CopyType type, uint32_t map, uint64_t owner) const
    {
        return state == SceneState::RUNNING
            && copyType == type
            && mapId == map
            && ownerId == owner
            && !isFull();
    }

    /** @brief 更新当前副本人数 */
    void setPlayerCount(uint32_t n) { playerCount = n; }

    /** @brief 更新副本状态 */
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
