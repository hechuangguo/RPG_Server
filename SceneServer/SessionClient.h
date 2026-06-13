/**
 * @file    SessionClient.h
 * @brief   SceneServer → SessionServer 出站客户端
 *
 * 场景注册/注销、副本创建请求；连接未就绪时缓存待注册场景。
 */

#pragma once

#include "ScenePeerClient.h"
#include "../protocal/InternalMsg.h"
#include "Scene.h"

#include <vector>

/**
 * @brief SceneServer 到 SessionServer 的出站连接与场景注册
 */
class SessionClient : public ScenePeerClient
{
public:
    SessionClient();

    /**
     * @brief 向 Session 注册场景（未连接时入队，OnConnect 后 flush）
     * @param sceneServerId 本 Scene 进程 ID
     * @param scene 已启动场景实例
     */
    void registerScene(uint32_t sceneServerId, const Scene& scene);

    /** @brief 向 Session 注销场景 */
    void unregisterScene(uint32_t sceneServerId, const Scene& scene);

    /** @brief 请求 Session 创建副本 */
    void requestCopyCreate(uint32_t sceneServerId, CopyType copyType, uint32_t mapId,
                           uint64_t ownerId, const std::string& mapName,
                           const std::string& mapFile, uint32_t maxPlayer);

    /** @brief 处理 SES_SCENE_REGISTER_RSP */
    void onRegisterRsp(const char* data, uint16_t len);

    /** @brief 连接就绪后重发待注册场景 */
    void flushPendingRegistrations();

private:
    std::vector<Msg_SES_SceneRegisterReq> pendingRegs; /**< 待注册队列 */
    uint32_t boundSceneServerId = 0;                   /**< 本进程 sceneServerId */
};
