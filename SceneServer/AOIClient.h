/**
 * @file    AOIClient.h
 * @brief   SceneServer → AOIServer 出站客户端
 *
 * 场景注册/注销与实体 enter/leave/move。
 */

#pragma once

#include "ScenePeerClient.h"
#include "../protocal/InternalMsg.h"
#include "Scene.h"
#include "SceneEntry.h"

#include <vector>

/**
 * @brief SceneServer 到 AOIServer 的出站连接
 */
class AOIClient : public ScenePeerClient
{
public:
    AOIClient();

    /** @brief 向 AOI 注册场景实例 */
    void registerScene(uint32_t sceneServerId, const Scene& scene);

    /** @brief 向 AOI 注销场景实例 */
    void unregisterScene(const Scene& scene);

    /** @brief 实体进入 AOI 视野 */
    void enterEntity(const SceneEntry& entry, uint8_t entityType);

    /** @brief 实体离开 AOI 视野 */
    void leaveEntity(EntryID entityId);

    /** @brief 实体移动同步到 AOI */
    void moveEntity(EntryID entityId, uint32_t mapId, float x, float y, float z,
                    float dir, uint8_t entityType);

    /** @brief 连接就绪后重发待注册场景 */
    void flushPendingRegistrations();

private:
    std::vector<Msg_AOI_SceneRegister> pendingRegs; /**< 待注册队列 */
};
