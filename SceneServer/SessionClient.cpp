/**
 * @file    SessionClient.cpp
 * @brief   SessionClient 实现
 */

#include "SessionClient.h"
#include "../protocal/InternalMsg.h"
#include "../sdk/util/WireStringUtil.h"
#include "../sdk/log/Logger.h"

SessionClient::SessionClient()
    : ScenePeerClient("SessionClient")
{
    setOnConnected([this]() { flushPendingRegistrations(); });
}

void SessionClient::registerScene(uint32_t sceneServerId, const Scene& scene)
{
    boundSceneServerId = sceneServerId;

    Msg_SES_SceneRegisterReq req{};
    req.sceneServerId = sceneServerId;
    req.sceneInstanceId = scene.getSceneInstanceId();
    req.mapId = scene.getMapId();
    req.sceneKind = static_cast<uint8_t>(scene.getSceneKind());
    req.maxPlayer = scene.getMaxPlayer();
    copyToWire(req.mapName, sizeof(req.mapName), scene.getMapName().c_str());
    copyToWire(req.mapFile, sizeof(req.mapFile), scene.getMapFile().c_str());
    lastRegAttempts[req.sceneInstanceId] = req;

    if (!isConnected())
    {
        pendingRegs.push_back(req);
        LOG_WARN("会话客户端: 场景注册已入队 instance=%llu map=%u",
                 req.sceneInstanceId, req.mapId);
        return;
    }

    if (!sendMsg(static_cast<uint16_t>(InternalMsgID::SES_SCENE_REGISTER_REQ),
                 reinterpret_cast<char*>(&req), sizeof(req)))
    {
        pendingRegs.push_back(req);
        return;
    }

    LOG_INFO("会话客户端注册场景: instance=%llu map=%u",
             req.sceneInstanceId, req.mapId);
}

void SessionClient::unregisterScene(uint32_t sceneServerId, const Scene& scene)
{
    Msg_SES_SceneUnregister req{};
    req.sceneInstanceId = scene.getSceneInstanceId();
    req.sceneServerId = sceneServerId;
    sendMsg(static_cast<uint16_t>(InternalMsgID::SES_SCENE_UNREGISTER),
            reinterpret_cast<char*>(&req), sizeof(req));
}

void SessionClient::requestCopyCreate(uint32_t sceneServerId, CopyType copyType,
                                      uint32_t mapId, uint64_t ownerId,
                                      const std::string& mapName,
                                      const std::string& mapFile, uint32_t maxPlayer)
{
    Msg_SES_CopyCreateReq req{};
    req.reqSceneServerId = sceneServerId;
    req.copyType = static_cast<uint32_t>(copyType);
    req.mapId = mapId;
    req.ownerId = ownerId;
    req.maxPlayer = maxPlayer;
    copyToWire(req.mapName, sizeof(req.mapName), mapName.c_str());
    copyToWire(req.mapFile, sizeof(req.mapFile), mapFile.c_str());
    sendMsg(static_cast<uint16_t>(InternalMsgID::SES_COPY_CREATE_REQ),
            reinterpret_cast<char*>(&req), sizeof(req));
    LOG_INFO("会话客户端副本创建请求: type=%u map=%u owner=%llu",
             req.copyType, mapId, ownerId);
}

void SessionClient::onRegisterRsp(const char* data, uint16_t len)
{
    if (len < sizeof(Msg_SES_SceneRegisterRsp))
        return;
    const auto* rsp = reinterpret_cast<const Msg_SES_SceneRegisterRsp*>(data);
    if (rsp->code != 0)
    {
        LOG_ERR("会话客户端注册场景失败: instance=%llu code=%d，将重试",
                rsp->sceneInstanceId, rsp->code);
        auto it = lastRegAttempts.find(rsp->sceneInstanceId);
        if (it != lastRegAttempts.end())
            pendingRegs.push_back(it->second);
        return;
    }
    LOG_INFO("会话客户端注册场景成功: instance=%llu", rsp->sceneInstanceId);
}

void SessionClient::reportMapLoad(uint32_t sceneServerId, uint32_t mapId, uint32_t playerCount)
{
    Msg_SES_SceneMapLoadReport rpt{};
    rpt.sceneServerId = sceneServerId;
    rpt.mapId = mapId;
    rpt.playerCount = playerCount;
    sendMsg(static_cast<uint16_t>(InternalMsgID::SES_SCENE_MAP_LOAD_REPORT),
            reinterpret_cast<char*>(&rpt), sizeof(rpt));
}

void SessionClient::reportServerLoad(uint32_t sceneServerId, uint32_t totalPlayers)
{
    Msg_SES_SceneMapLoadReport rpt{};
    rpt.sceneServerId = sceneServerId;
    rpt.mapId = 0;
    rpt.playerCount = totalPlayers;
    sendMsg(static_cast<uint16_t>(InternalMsgID::SES_SCENE_MAP_LOAD_REPORT),
            reinterpret_cast<char*>(&rpt), sizeof(rpt));
}

void SessionClient::flushPendingRegistrations()
{
    if (pendingRegs.empty() || !isConnected())
        return;

    auto queued = std::move(pendingRegs);
    pendingRegs.clear();
    for (const auto& req : queued)
    {
        sendMsg(static_cast<uint16_t>(InternalMsgID::SES_SCENE_REGISTER_REQ),
                reinterpret_cast<const char*>(&req), sizeof(req));
        LOG_INFO("会话客户端补发待注册场景: instance=%llu map=%u",
                 req.sceneInstanceId, req.mapId);
    }
}
