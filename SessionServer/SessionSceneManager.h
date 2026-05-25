/**
 * @file    SessionSceneManager.h
 * @brief  SessionServer 全区场景管理（普通地图 + 副本 + SceneServer 负载）
 */

#pragma once
#include "SessionScene.h"
#include "SessionCopyScene.h"
#include "../protocal/InternalMsg.h"
#include "../sdk/net/NetDefine.h"
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

/** @brief Session 记录的 SceneServer 节点信息 */
struct SessionSceneServerNode
{
    ConnID   connId       = INVALID_CONN_ID;
    uint32_t sceneServerId = 0;
    uint32_t sceneCount   = 0;
    uint32_t playerCount  = 0;
    bool     alive        = true;
};

/**
 * @brief 管理全区所有 SceneServer 上的场景与副本
 */
class SessionSceneManager
{
public:
    void bindSceneServer(ConnID connId, uint32_t sceneServerId);
    void unbindConn(ConnID connId);

    ConnID findConnBySceneServerId(uint32_t sceneServerId) const;

    bool registerScene(ConnID connId, const Msg_SES_SceneRegisterReq& req);
    bool unregisterScene(uint64_t sceneInstanceId, uint32_t sceneServerId);

    SessionCopyScene* findReusableCopy(CopyType type, uint32_t mapId, uint64_t ownerId);

    SessionCopyScene* createCopyRecord(uint32_t sceneServerId, uint64_t copyInstanceId,
                                       const Msg_SES_CopyCreateReq& req);

    SessionCopyScene* findCopy(uint64_t copyInstanceId) const;
    SessionScene* findNormalScene(uint64_t sceneInstanceId) const;

    /** @brief 按负载选择 SceneServer（场景数 + 玩家数加权） */
    uint32_t pickSceneServerId() const;

    uint64_t generateCopyInstanceId();

    size_t getNormalSceneCount() const { return m_normalScenes.size(); }
    size_t getCopySceneCount() const { return m_copyScenes.size(); }

private:
    void adjustServerSceneCount(uint32_t sceneServerId, int delta);

    std::unordered_map<uint32_t, SessionSceneServerNode>  m_sceneServers;
    std::unordered_map<ConnID, uint32_t>                  m_connToServerId;
    std::unordered_map<uint64_t, SessionScene>            m_normalScenes;
    std::unordered_map<uint64_t, SessionCopyScene>        m_copyScenes;
    uint64_t m_nextCopyInstanceId = 0x100000000ULL;
};
