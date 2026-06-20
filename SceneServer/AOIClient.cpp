/**
 * @file    AOIClient.cpp
 * @brief   AOIClient 实现
 */

#include "AOIClient.h"
#include "../sdk/log/Logger.h"

AOIClient::AOIClient()
    : ScenePeerClient("AOIClient")
{
    setOnConnected([this]() { flushPendingRegistrations(); });
}

void AOIClient::registerScene(uint32_t sceneServerId, const Scene& scene)
{
    Msg_AOI_SceneRegister req{};
    req.sceneServerId = sceneServerId;
    req.sceneInstanceId = scene.getSceneInstanceId();
    req.mapId = scene.getMapId();
    req.sceneKind = static_cast<uint8_t>(scene.getSceneKind());
    req.maxPlayer = scene.getMaxPlayer();
    req.aoiGridSize = scene.getAoiGridSize();

    if (!isConnected())
    {
        pendingRegs.push_back(req);
        LOG_WARN("视野客户端: 场景注册已入队 instance=%llu map=%u",
                 req.sceneInstanceId, req.mapId);
        return;
    }

    if (!sendMsg(static_cast<uint16_t>(InternalMsgID::AOI_SCENE_REGISTER),
                 reinterpret_cast<char*>(&req), sizeof(req)))
    {
        pendingRegs.push_back(req);
        return;
    }

    LOG_INFO("视野客户端注册场景: instance=%llu map=%u",
             req.sceneInstanceId, req.mapId);
}

void AOIClient::unregisterScene(const Scene& scene)
{
    Msg_AOI_SceneUnregister req{};
    req.sceneInstanceId = scene.getSceneInstanceId();
    sendMsg(static_cast<uint16_t>(InternalMsgID::AOI_SCENE_UNREGISTER),
            reinterpret_cast<char*>(&req), sizeof(req));
}

void AOIClient::enterEntity(const SceneEntry& entry, uint8_t entityType)
{
    Msg_AOI_Move req{};
    req.entityID = entry.getEntryId();
    req.mapID = entry.getMapId();
    req.x = entry.getPosX();
    req.y = entry.getPosY();
    req.z = entry.getPosZ();
    req.dir = 0.f;
    req.entityType = entityType;
    sendMsg(static_cast<uint16_t>(InternalMsgID::AOI_ENTER_REQ),
            reinterpret_cast<char*>(&req), sizeof(req));
}

void AOIClient::leaveEntity(EntryID entityId)
{
    sendMsg(static_cast<uint16_t>(InternalMsgID::AOI_LEAVE_REQ),
            reinterpret_cast<const char*>(&entityId), sizeof(entityId));
}

void AOIClient::moveEntity(EntryID entityId, uint32_t mapId, float x, float y, float z,
                           float dir, uint8_t entityType)
{
    Msg_AOI_Move req{};
    req.entityID = entityId;
    req.mapID = mapId;
    req.x = x;
    req.y = y;
    req.z = z;
    req.dir = dir;
    req.entityType = entityType;
    sendMsg(static_cast<uint16_t>(InternalMsgID::AOI_MOVE_REQ),
            reinterpret_cast<char*>(&req), sizeof(req));
}

void AOIClient::flushPendingRegistrations()
{
    if (pendingRegs.empty() || !isConnected())
        return;

    auto queued = std::move(pendingRegs);
    pendingRegs.clear();
    for (const auto& req : queued)
    {
        sendMsg(static_cast<uint16_t>(InternalMsgID::AOI_SCENE_REGISTER),
                reinterpret_cast<const char*>(&req), sizeof(req));
        LOG_INFO("视野客户端补发待注册场景: instance=%llu map=%u",
                 req.sceneInstanceId, req.mapId);
    }
}
