/**
 * @file SceneServer.cpp
 * @brief SceneServer 实现：移动/聊天/NPC 等客户端 Protobuf handler 与 AOI 协作
 */

#include "SceneServer.h"
#include "SceneInternMsgRegister.h"
#include "SceneClientMsgRegister.h"
#include "../sdk/net/MsgIngress.h"
#include "../sdk/net/NetTls.h"
#include "ScenePeerClient.h"
#include "../sdk/util/ServerBootstrap.h"
#include "../sdk/util/LoginFlowLog.h"
#include "../sdk/util/GameZoneExternSender.h"
#include "SceneLoginMsg.h"
#include "SceneUserManager.h"
#include "SceneNpcManager.h"
#include "SceneManager.h"
#include "MoveValidator.h"
#include "../sdk/net/ClientProtoWire.h"
#include "../sdk/net/ClientWireSend.h"
#include "MapDataMsg.pb.h"
#include "ChatMsg.pb.h"
#include "NpcMsg.pb.h"
#include "LuaManager.h"

#include <cstdio>
#include <unordered_map>

namespace
{

void sendSpawnProto(SceneServer& server, uint32_t clientConn, const SceneEntry& entry,
                    uint8_t entityType, uint32_t modelId = 0, uint32_t animState = 0)
{
    rpg::mapdata::S2CSpawnEntity msg;
    fillProtoSpawnEntity(entry.getEntryId(), entry.getName(), entry.getLevel(),
                         entry.getPosX(), entry.getPosY(), entry.getPosZ(), 0.f, entityType,
                         modelId, animState, msg);
    std::string body;
    if (!serializeSpawnEntity(msg, body))
        return;
    server.SendToClient(clientConn, static_cast<uint8_t>(rpg::client::SCENE),
                        static_cast<uint8_t>(rpg::mapdata::S2C_SPAWN_ENTITY),
                        body.data(), static_cast<uint16_t>(body.size()));
}

void broadcastSpawnProto(SceneServer& server, uint32_t mapId, UserID exclude,
                         const SceneEntry& entry, uint8_t entityType)
{
    rpg::mapdata::S2CSpawnEntity msg;
    fillProtoSpawnEntity(entry.getEntryId(), entry.getName(), entry.getLevel(),
                         entry.getPosX(), entry.getPosY(), entry.getPosZ(), 0.f, entityType,
                         0, 0, msg);
    std::string body;
    if (!serializeSpawnEntity(msg, body))
        return;
    server.BroadcastToMap(mapId, exclude, static_cast<uint8_t>(rpg::client::SCENE),
                          static_cast<uint8_t>(rpg::mapdata::S2C_SPAWN_ENTITY),
                          body.data(), static_cast<uint16_t>(body.size()));
}

void broadcastDespawnProto(SceneServer& server, uint32_t mapId, UserID exclude, uint64_t entityId)
{
    rpg::mapdata::S2CDespawnEntity msg;
    msg.set_entity_id(entityId);
    std::string body;
    if (!serializeDespawnEntity(msg, body))
        return;
    server.BroadcastToMap(mapId, exclude, static_cast<uint8_t>(rpg::client::SCENE),
                          static_cast<uint8_t>(rpg::mapdata::S2C_DESPAWN_ENTITY),
                          body.data(), static_cast<uint16_t>(body.size()));
}

void broadcastMoveProto(SceneServer& server, uint32_t mapId, UserID exclude,
                        uint64_t userId, float x, float y, float z, float dir, uint8_t moveType)
{
    rpg::mapdata::S2CMoveNotify msg;
    msg.set_user_id(userId);
    msg.mutable_pos()->set_x(x);
    msg.mutable_pos()->set_y(y);
    msg.mutable_pos()->set_z(z);
    msg.set_dir(dir);
    msg.set_move_type(moveType == 1 ? rpg::mapdata::MOVE_TYPE_RUN : rpg::mapdata::MOVE_TYPE_WALK);
    std::string body;
    if (!serializeMoveNotify(msg, body))
        return;
    server.BroadcastToMap(mapId, exclude, static_cast<uint8_t>(rpg::client::SCENE),
                          static_cast<uint8_t>(rpg::mapdata::S2C_MOVE_NOTIFY),
                          body.data(), static_cast<uint16_t>(body.size()));
}

} // namespace

SceneServer::SceneServer()
    : m_server(this)
    , m_superClient(&m_superUpstreamCb)
    , m_sceneID(0)
    , m_externSender(m_superClient, SubServerType::SCENE, 0)
{
}

bool SceneServer::Init(const std::string& ip, uint16_t port,
                       const ServerConfig& cfg, const SceneServerInfo& sceneInfo,
                       const ServerList& list, uint32_t selfId)
{
    Logger::Instance().SetServerName("SceneServer");
    m_sceneID = sceneInfo.sceneID;
    if (const ServerEntry* self = list.find(SubServerType::SCENE, selfId))
        m_self = *self;
    else if (const ServerEntry* first = list.findFirst(SubServerType::SCENE))
        m_self = *first;
    m_externSender.setSelfId(m_self.id ? m_self.id : selfId);
    ServerBootstrap::bindRemoteLog(m_externSender, SubServerType::SCENE);
    wireTlsServer(m_server);
    if (!m_server.Start(ip, port)) { LOG_FATAL("场景服启动失败"); return false; }

    wireTlsClient(m_superClient);
    m_superClient.Connect(cfg.superIP, (uint16_t)cfg.superPort);
    if (const ServerEntry* ses = list.findFirst(SubServerType::SESSION))
        m_sessionClient.connect(ses->ip, ses->port);
    if (const ServerEntry* rec = list.findFirst(SubServerType::RECORD))
        m_recordClient.connect(rec->ip, rec->port);
    if (const ServerEntry* aoi = list.findFirst(SubServerType::AOI))
        m_aoiClient.connect(aoi->ip, aoi->port);
    m_listenPort = port;

    SceneManager::Instance().setStartedCallback([this](Scene& scene) { onSceneStarted(scene); });
    SceneManager::Instance().setStoppedCallback([this](Scene& scene) { onSceneStopped(scene); });

    if (!SceneManager::Instance().createNormalScenesFromConfig(m_sceneID, sceneInfo))
        LOG_WARN("部分普通场景启动失败: sceneID=%u", m_sceneID);

    initMapNpcs();

    if (!LuaManager::Instance().init())
        LOG_WARN("场景服脚本初始化失败");
    registerHandlers();

    TimerMgr::Instance().Register(500, 0, [this] { registerToSuper(); });
    TimerMgr::Instance().Register(10000, 10000, [this] { sendHeartbeat(); });
    TimerMgr::Instance().Register(1000, 1000, [this] { onTick(); });
    TimerMgr::Instance().Register(30000, 30000, [this] { reportMapLoadToSession(); });

    LOG_INFO("场景服启动完成: sceneID=%u %s:%d", m_sceneID, ip.c_str(), port);
    return true;
}

void SceneServer::Run()
{
    while (true)
    {
        m_server.Poll(10);
        TimerMgr::Instance().Update();
        m_superClient.Poll(0);
        m_sessionClient.poll();
        m_recordClient.poll();
        m_aoiClient.poll();
    }
}

void SceneServer::requestCreateCopy(CopyType copyType, uint32_t mapId, uint64_t ownerId,
                                    const std::string& mapName, const std::string& mapFile,
                                    uint32_t maxPlayer)
{
    m_sessionClient.requestCopyCreate(m_sceneID, copyType, mapId, ownerId, mapName, mapFile, maxPlayer);
    LOG_INFO("已发送副本创建请求: type=%u map=%u owner=%llu",
             static_cast<uint32_t>(copyType), mapId, ownerId);
}

void SceneServer::OnConnect(ConnID id)
{
    if (m_gatewayInboundConn == INVALID_CONN_ID)
        m_gatewayInboundConn = id;
    LOG_INFO("场景服入站连接建立: conn=%u (gateway=%u)", id, m_gatewayInboundConn);
}

void SceneServer::OnDisconnect(ConnID id)
{
    if (id == m_gatewayInboundConn)
        m_gatewayInboundConn = INVALID_CONN_ID;
    LOG_WARN("场景服连接断开: conn=%u", id);
}

void SceneServer::OnMessage(ConnID id, uint8_t module, uint8_t sub,
                            const char* data, uint16_t len)
{
    MsgIngress::dispatchInternal(id, module, sub, data, len);
}

void SceneServer::registerHandlers()
{
    SceneInternMsgRegister(*this);
    SceneClientMsgRegister(*this);
}

void SceneServer::onUserEnter(ConnID /*fromConn*/, const Msg_SCE_UserEnterReq& req)
{
    LOG_INFO("用户进入场景: userID=%llu mapID=%u clientConn=%u",
             req.userID, req.mapID, req.gatewayClientConnID);

    if (auto existing = SceneUserManager::Instance().findUser(req.userID))
    {
        LOG_WARN("同 userID 重复进入，先清理旧会话: userID=%llu", req.userID);
        if (auto scene = SceneManager::Instance().findNormalSceneByMapId(existing->Base().mapID))
            scene->removePlayer(req.userID);
        existing->onOffline();
        if (existing->needSave())
        {
            sendCharBaseToRecord(*existing);
            existing->save();
        }
        m_aoiClient.leaveEntity(req.userID);
        callLuaOnLeave(req.userID);
        SceneUserManager::Instance().removeUser(req.userID);
    }

    uint32_t mapID = req.mapID ? req.mapID : 1001;
    auto scene = SceneManager::Instance().findNormalSceneByMapId(mapID);
    if (!scene)
    {
        LOG_WARN("场景服未找到地图: map=%u sceneID=%u", mapID, m_sceneID);
        logLoginFlow(LoginFlowPhase::SCENE_ENTER, 0, req.userID, req.gatewayClientConnID,
                     -1, "地图不存在");
        sendUserEnterRsp(&req, -1);
        return;
    }

    UserBase base;
    base.userID = req.userID;
    base.name = req.name;
    base.level = req.level;
    base.vocation = req.vocation;
    base.sex = req.sex;
    base.mapID = mapID;
    base.posX = req.x;
    base.posY = req.y;
    base.posZ = req.z;
    base.hp = req.hp;
    base.maxHP = req.maxHP;
    base.mp = req.mp;
    base.maxMP = req.maxMP;
    base.gold = req.gold;

    auto user = SceneUser::create(base);
    user->init();
    user->load();
    user->setGatewayClientConn(req.gatewayClientConnID);
    user->onOnline();
    SceneUserManager::Instance().addUser(req.userID, user);
    scene->addPlayer(req.userID);

    m_aoiClient.enterEntity(*user, 0);

    notifyExistingPlayersOnEnter(*user);
    sendUserEnterRsp(&req, 0);
    char detail[32];
    snprintf(detail, sizeof(detail), "入场成功 map=%u", mapID);
    logLoginFlow(LoginFlowPhase::SCENE_ENTER, 0, req.userID, req.gatewayClientConnID,
                 0, detail);
    callLuaOnEnter(req.userID, mapID);
}

void SceneServer::sendUserEnterRsp(const Msg_SCE_UserEnterReq* req, int32_t code)
{
    Msg_SCE_UserEnterRsp rsp{};
    rsp.code = code;
    rsp.userID = req->userID;
    rsp.gatewayClientConnID = req->gatewayClientConnID;
    rsp.mapID = req->mapID ? req->mapID : 1001;
    m_superClient.SendMsg((uint16_t)InternalMsgID::SCE_USER_ENTER_RSP,
                          reinterpret_cast<char*>(&rsp), sizeof(rsp));
}

void SceneServer::notifyExistingPlayersOnEnter(const SceneUser& entering)
{
    const auto& base = entering.Base();

    SceneUserManager::Instance().forEach([&](UserID rid, const std::shared_ptr<SceneUser>& user)
    {
        if (rid == base.userID) return;
        if (user->Base().mapID != base.mapID) return;
        if (user->getGatewayClientConn() == 0) return;

        sendSpawnProto(*this, user->getGatewayClientConn(), entering, 0);
        sendSpawnProto(*this, entering.getGatewayClientConn(), *user, 0);
    });

    SceneNpcManager::Instance().forEach([&](EntryID /*npcId*/, const std::shared_ptr<SceneNpc>& npc)
    {
        if (!npc || npc->getMapId() != base.mapID) return;
        if (!npc->isAlive()) return;
        sendSpawnProto(*this, entering.getGatewayClientConn(), *npc, 1);
    });
}

void SceneServer::onUserLeave(ConnID /*fromConn*/, const char* data, uint16_t len)
{
    if (len < sizeof(UserID)) return;
    UserID uid = *reinterpret_cast<const UserID*>(data);
    auto user = SceneUserManager::Instance().findUser(uid);
    if (!user) return;

    if (auto scene = SceneManager::Instance().findNormalSceneByMapId(user->Base().mapID))
        scene->removePlayer(uid);

    user->onOffline();
    if (user->needSave())
    {
        sendCharBaseToRecord(*user);
        user->save();
    }

    m_aoiClient.leaveEntity(uid);
    callLuaOnLeave(uid);
    SceneUserManager::Instance().removeUser(uid);
    LOG_INFO("用户离开场景: userID=%llu", uid);
    logLoginFlow(LoginFlowPhase::CHAR_LEAVE, 0, uid, user->getGatewayClientConn(), 0,
                 "Scene清理");
}

void SceneServer::sendCharBaseToRecord(const SceneUser& user)
{
    m_recordClient.saveUser(user);
}

void SceneServer::onViewNotify(ConnID /*fromConn*/, const char* data, uint16_t len)
{
    if (len == sizeof(Msg_AOI_Move))
    {
        const auto* move = reinterpret_cast<const Msg_AOI_Move*>(data);
        auto user = SceneUserManager::Instance().findUser(move->entityID);
        if (user)
        {
            user->Base().posX = move->x;
            user->Base().posY = move->y;
            user->Base().posZ = move->z;
            user->markDirty();
        }
        else
        {
            auto npc = SceneNpcManager::Instance().findNpc(move->entityID);
            if (npc)
                npc->setPos(move->x, move->y, move->z);
        }

        broadcastMoveProto(*this, move->mapID, move->entityID, move->entityID,
                           move->x, move->y, move->z, move->dir, 0);
        return;
    }

    if (len < sizeof(uint64_t) + 1) return;
    uint64_t entityID = 0;
    memcpy(&entityID, data, sizeof(uint64_t));
    bool enter = data[sizeof(uint64_t)] != 0;

    auto user = SceneUserManager::Instance().findUser(entityID);
    auto npc = SceneNpcManager::Instance().findNpc(entityID);

    uint32_t mapId = 0;
    if (user)
        mapId = user->Base().mapID;
    else if (npc)
        mapId = npc->getMapId();
    else
        return;

    if (enter)
    {
        if (user)
            broadcastSpawnProto(*this, mapId, entityID, *user, 0);
        else
            broadcastSpawnProto(*this, mapId, entityID, *npc, 1);
    }
    else
    {
        broadcastDespawnProto(*this, mapId, entityID, entityID);
    }
}

void SceneServer::onMoveReq(uint32_t clientConnID, const char* data, uint16_t len)
{
    rpg::mapdata::C2SMoveReq req;
    if (!parseMoveReq(data, len, req))
        return;
    auto user = SceneUserManager::Instance().findUserByClientConn(clientConnID);
    if (!user)
        return;
    if (req.user_id() != 0 && req.user_id() != user->GetID())
    {
        LOG_WARN("移动请求 user_id 与连接不匹配: conn=%u req=%llu actual=%llu",
                 clientConnID,
                 static_cast<unsigned long long>(req.user_id()),
                 static_cast<unsigned long long>(user->GetID()));
        return;
    }

    const float dstX = req.has_pos() ? req.pos().x() : user->Base().posX;
    const float dstY = req.has_pos() ? req.pos().y() : user->Base().posY;
    const float dstZ = req.has_pos() ? req.pos().z() : user->Base().posZ;
    const uint8_t moveType = req.move_type() == rpg::mapdata::MOVE_TYPE_RUN ? 1 : 0;

    auto scene = SceneManager::Instance().findNormalSceneByMapId(user->Base().mapID);
    MapRuntimeData* mapData = scene && scene->getMapData() ? scene->getMapData().get() : nullptr;

    std::string reason;
    const auto vr = validateMoveRequest(mapData, user->Base().posX, user->Base().posY,
                                        user->Base().posZ, dstX, dstY, dstZ, moveType, &reason);
    if (vr != MoveValidateResult::OK)
    {
        LOG_WARN("移动校验拒绝 user=%llu map=%u: %s", req.user_id(), user->Base().mapID,
                 reason.c_str());
        return;
    }

    user->Base().posX = dstX;
    user->Base().posY = dstY;
    user->Base().posZ = dstZ;
    user->markDirty();

    m_aoiClient.moveEntity(req.user_id(), user->Base().mapID, dstX, dstY, dstZ,
                           req.dir(), 0);
}

void SceneServer::onChatReq(uint32_t clientConnID, const char* data, uint16_t len)
{
    rpg::chat::C2SChatReq req;
    if (!parseProto(data, len, req))
        return;
    auto user = SceneUserManager::Instance().findUserByClientConn(clientConnID);
    if (!user) return;

    rpg::chat::S2CChatNotify notify;
    notify.set_from_id(user->GetID());
    notify.set_from_name(user->GetName());
    notify.set_channel(req.channel());
    notify.set_content(req.content());
    std::string body;
    if (!serializeProto(notify, body))
    {
        LOG_WARN("聊天广播序列化失败: user=%llu", user->GetID());
        return;
    }
    BroadcastToMap(user->Base().mapID, user->GetID(),
                   static_cast<uint8_t>(rpg::client::CHAT),
                   static_cast<uint8_t>(rpg::chat::S2C_CHAT_NOTIFY),
                   body.data(), static_cast<uint16_t>(body.size()));
}

void SceneServer::onNpcTalkReq(uint32_t clientConnID, const char* data, uint16_t len)
{
    rpg::npc::C2SNpcTalkReq req;
    if (!parseProto(data, len, req))
        return;

    auto user = SceneUserManager::Instance().findUserByClientConn(clientConnID);
    if (!user || user->getGatewayClientConn() == 0)
        return;

    auto npc = SceneNpcManager::Instance().findNpc(req.npc_id());
    if (!npc || npc->isDead())
    {
        sendNpcTalkError(user->getGatewayClientConn(), req.npc_id(), 1);
        return;
    }

    if (npc->getMapId() != user->Base().mapID)
    {
        sendNpcTalkError(user->getGatewayClientConn(), req.npc_id(), 2);
        return;
    }

    const bool ok = LuaManager::Instance().callScriptBool(npc.get(), "OnNpcTalk", {
        LuaArg::integer(static_cast<int64_t>(user->GetID())),
        LuaArg::integer(req.dialog_step()),
        LuaArg::integer(npc->getTemplateId()),
    });

    if (!ok)
        sendNpcTalkError(user->getGatewayClientConn(), req.npc_id(), 3);
}

void SceneServer::sendNpcTalkError(uint32_t clientConnID, uint64_t npcId, int32_t code)
{
    rpg::npc::S2CNpcTalkRsp rsp;
    rsp.set_code(code);
    rsp.set_npc_id(npcId);
    rsp.set_dialog_step(0);
    std::string body;
    if (!serializeProto(rsp, body))
        return;
    SendToClient(clientConnID, static_cast<uint8_t>(rpg::client::NPC),
                 static_cast<uint8_t>(rpg::npc::S2C_NPC_TALK_RSP),
                 body.data(), static_cast<uint16_t>(body.size()));
}

bool SceneServer::SendToClient(uint32_t clientConnID, uint8_t module, uint8_t sub,
                               const char* data, uint16_t len)
{
    if (relaySendToClientViaGateway(m_server, m_gatewayInboundConn,
                                    clientConnID, module, sub, data, len))
        return true;
    LOG_WARN("下发客户端失败: 无网关入站连接 clientConn=%u", clientConnID);
    return false;
}

bool SceneServer::SendToClient(uint32_t clientConnID, uint16_t flatMsgId,
                               const char* data, uint16_t len)
{
    return SendToClient(clientConnID,
                        static_cast<uint8_t>(flatMsgId >> 8),
                        static_cast<uint8_t>(flatMsgId & 0xFF),
                        data, len);
}

void SceneServer::BroadcastToMap(uint32_t mapID, UserID excludeUserID,
                                 uint8_t module, uint8_t sub,
                                 const char* data, uint16_t len)
{
    SceneUserManager::Instance().forEach([&](UserID uid, const std::shared_ptr<SceneUser>& user)
    {
        if (mapID != 0 && user->Base().mapID != mapID) return;
        if (uid == excludeUserID) return;
        if (user->getGatewayClientConn() == 0) return;
        SendToClient(user->getGatewayClientConn(), module, sub, data, len);
    });
}

void SceneServer::callLuaOnEnter(UserID userID, uint32_t mapID)
{
    LuaManager::Instance().callGlobalVoid("OnUserEnter", {
        LuaArg::integer(static_cast<int64_t>(userID)),
        LuaArg::integer(mapID),
    });
}

void SceneServer::callLuaOnLeave(UserID userID)
{
    LuaManager::Instance().callGlobalVoid("OnUserLeave", {
        LuaArg::integer(static_cast<int64_t>(userID)),
    });
}

void SceneServer::reportMapLoadToSession()
{
    std::unordered_map<uint32_t, uint32_t> mapCounts;
    SceneUserManager::Instance().forEach([&](UserID /*uid*/, const std::shared_ptr<SceneUser>& user) {
        if (user)
            ++mapCounts[user->getMapId()];
    });

    for (const auto& [mapId, count] : mapCounts)
        m_sessionClient.reportMapLoad(m_sceneID, mapId, count);

    m_sessionClient.reportServerLoad(m_sceneID,
                                     static_cast<uint32_t>(SceneUserManager::Instance().getUserCount()));
}

void SceneServer::onTick()
{
    uint64_t now = TimerMgr::NowMs();
    SceneUserManager::Instance().forEachMutable([&](UserID /*uid*/, SceneUser& user)
    {
        user.loop(now);
    });
    SceneNpcManager::Instance().loopAll(now);
    LuaManager::Instance().callGlobalVoid("OnTick", { LuaArg::integer(static_cast<int64_t>(now)) });
}

void SceneServer::initMapNpcs()
{
    SceneManager::Instance().forEach([this](const std::shared_ptr<Scene>& scene)
    {
        if (!scene || scene->getSceneKind() != SceneKind::NORMAL)
            return;

        const uint32_t mapId = scene->getMapId();
        SceneNpcDef def{};
        def.npcId = 1000000ULL + mapId;
        def.templateId = 1;
        def.name = "新手引导官";
        def.level = 1;
        def.hp = 500;
        def.maxHp = 500;
        def.vitality = 100;
        def.maxVitality = 100;
        def.mapId = mapId;
        def.posX = 10.f;
        def.posY = 0.f;
        def.posZ = 10.f;
        def.respawnSec = 30;

        if (SceneNpcManager::Instance().createNpc(def))
        {
            LOG_INFO("怪物已生成: id=%llu map=%u", def.npcId, mapId);
            auto npc = SceneNpcManager::Instance().findNpc(def.npcId);
            if (npc)
                notifyNpcEnterAoi(*npc);
        }
    });
}

void SceneServer::onSceneStarted(Scene& scene)
{
    m_aoiClient.registerScene(m_sceneID, scene);
    m_sessionClient.registerScene(m_sceneID, scene);
    LOG_INFO("场景已注册到视野服+会话服: instance=%llu map=%u kind=%u",
             scene.getSceneInstanceId(), scene.getMapId(),
             static_cast<unsigned>(scene.getSceneKind()));
}

void SceneServer::onSceneStopped(Scene& scene)
{
    m_aoiClient.unregisterScene(scene);
    m_sessionClient.unregisterScene(m_sceneID, scene);
}

void SceneServer::onSceneRegisterRsp(ConnID /*fromConn*/, const char* data, uint16_t len)
{
    m_sessionClient.onRegisterRsp(data, len);
}

void SceneServer::onSaveUserRsp(ConnID /*fromConn*/, const char* data, uint16_t len)
{
    m_recordClient.onSaveRsp(data, len);
}

void SceneServer::onCopyCreateRsp(ConnID /*fromConn*/, const Msg_SES_CopyCreateRsp& rsp)
{
    LOG_INFO("副本创建响应: code=%d targetServer=%u instance=%llu reused=%u",
             rsp.code, rsp.targetSceneServerId, rsp.copyInstanceId, rsp.reused);
}

void SceneServer::onCopyCreateCmd(ConnID /*fromConn*/, const Msg_SES_CopyCreateCmd& cmd)
{
    CopySceneDef def{};
    def.copyInstanceId = cmd.copyInstanceId;
    def.copyType = static_cast<CopyType>(cmd.copyType);
    def.mapId = cmd.mapId;
    def.ownerId = cmd.ownerId;
    def.maxPlayer = cmd.maxPlayer;
    def.mapName = cmd.mapName;
    def.mapFile = cmd.mapFile;

    if (SceneManager::Instance().createCopyScene(m_sceneID, def))
        LOG_INFO("本地创建副本成功: instance=%llu", cmd.copyInstanceId);
    else
        LOG_ERR("本地创建副本失败: instance=%llu", cmd.copyInstanceId);
}

void SceneServer::sendAoiEnter(const SceneEntry& entry, uint8_t entityType)
{
    m_aoiClient.enterEntity(entry, entityType);
}

void SceneServer::sendAoiLeave(EntryID entityId)
{
    m_aoiClient.leaveEntity(entityId);
}

void SceneServer::registerToSuper()
{
    Msg_S2S_Register reg{};
    reg.serverType = (uint8_t)SubServerType::SCENE;
    reg.serverID = m_sceneID;
    copyToWire(reg.ip, sizeof(reg.ip),
               m_self.ip.empty() ? "127.0.0.1" : m_self.ip.c_str());
    reg.port = m_listenPort;
    copyToWire(reg.name, sizeof(reg.name), m_self.name.c_str());
    m_superClient.SendMsg((uint16_t)InternalMsgID::S2S_REGISTER_REQ,
                          reinterpret_cast<char*>(&reg), sizeof(reg));
}

void SceneServer::sendHeartbeat()
{
    if (!m_superClient.canSend())
        return;
    Msg_S2S_Heartbeat hb{};
    hb.seq = ++m_hbSeq;
    hb.timestamp = TimerMgr::NowMs();
    m_superClient.SendMsg((uint16_t)InternalMsgID::S2S_HEARTBEAT,
                          reinterpret_cast<char*>(&hb), sizeof(hb));
}
