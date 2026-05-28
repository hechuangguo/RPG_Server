/**
 * @file    SessionSceneManager.h
 * @brief  SessionServer 全区场景管理（普通地图 + 副本 + SceneServer 负载）
 */

#pragma once
#include "SessionScene.h"
#include "SessionCopyScene.h"
#include "../protocal/InternalMsg.h"
#include "../sdk/net/NetDefine.h"
#include "../sdk/util/Singleton.h"
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

/** @brief Session 记录的 SceneServer 节点信息 */
struct SessionSceneServerNode
{
    ConnID   connId        = INVALID_CONN_ID; /**< SceneServer 连接 ID */
    uint32_t sceneServerId = 0;               /**< SceneServer 实例编号 */
    uint32_t sceneCount    = 0;               /**< 当前承载场景数 */
    uint32_t playerCount   = 0;               /**< 当前承载玩家数 */
    bool     alive         = true;            /**< 是否可用于新分配 */
};

/**
 * @brief 管理全区所有 SceneServer 上的场景与副本（单例）
 */
class SessionSceneManager : public LazySingleton<SessionSceneManager>
{
public:
    friend class LazySingleton<SessionSceneManager>;

    /** @brief 获取全局唯一实例 */
    static SessionSceneManager& Instance() { return LazySingleton<SessionSceneManager>::Instance(); }

    /** @brief 绑定 SceneServer 连接与 serverId 映射 */
    void bindSceneServer(ConnID connId, uint32_t sceneServerId);

    /** @brief 连接断开时清理 SceneServer 映射 */
    void unbindConn(ConnID connId);

    /** @brief 通过 SceneServer 编号反查连接 ID */
    ConnID findConnBySceneServerId(uint32_t sceneServerId) const;

    /** @brief 注册普通场景或副本场景到 Session 全局索引 */
    bool registerScene(ConnID connId, const Msg_SES_SceneRegisterReq& req);

    /** @brief 注销场景并回收对应统计信息 */
    bool unregisterScene(uint64_t sceneInstanceId, uint32_t sceneServerId);

    /** @brief 查找可复用副本（同类型同地图同 owner 且未满员） */
    SessionCopyScene* findReusableCopy(CopyType type, uint32_t mapId, uint64_t ownerId);

    /** @brief 创建副本记录并写入 Session 全局索引 */
    SessionCopyScene* createCopyRecord(uint32_t sceneServerId, uint64_t copyInstanceId,
                                       const Msg_SES_CopyCreateReq& req);

    /** @brief 按副本实例 ID 查副本记录 */
    SessionCopyScene* findCopy(uint64_t copyInstanceId) const;

    /** @brief 按场景实例 ID 查普通地图记录 */
    SessionScene* findNormalScene(uint64_t sceneInstanceId) const;

    /** @brief 按负载选择 SceneServer（场景数 + 玩家数加权） */
    uint32_t pickSceneServerId() const;

    /** @brief 生成全区唯一副本实例 ID */
    uint64_t generateCopyInstanceId();

    /** @brief 已注册普通场景数量 */
    size_t getNormalSceneCount() const { return m_normalScenes.size(); }

    /** @brief 已注册副本数量 */
    size_t getCopySceneCount() const { return m_copyScenes.size(); }

private:
    SessionSceneManager() = default;

    /** @brief 调整 SceneServer 的场景数量统计 */
    void adjustServerSceneCount(uint32_t sceneServerId, int delta);
    std::unordered_map<uint32_t, SessionSceneServerNode>  m_sceneServers;      /**< serverId -> 节点状态 */
    std::unordered_map<ConnID, uint32_t>                  m_connToServerId;    /**< connId -> serverId 反向索引 */
    std::unordered_map<uint64_t, SessionScene>            m_normalScenes;      /**< 普通场景表 */
    std::unordered_map<uint64_t, SessionCopyScene>        m_copyScenes;        /**< 副本场景表 */
    uint64_t m_nextCopyInstanceId = 0x100000000ULL;                            /**< 副本实例号自增游标 */
};
