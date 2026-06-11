/**
 * @file SceneServer.cpp
 * @brief SceneServer 非简单成员函数实现，降低头文件编译耦合。
 */

#include "SceneServer.h"
#include "../sdk/util/ServerBootstrap.h"
#include "SceneUserManager.h"
#include "SceneNpcManager.h"
#include "SceneManager.h"
#include "LuaManager.h"

SceneServer::SceneServer()
    : m_server(this)
    , m_superClient(this)
    , m_sessionClient(this)
    , m_recordClient(this)
    , m_aoiClient(this)
    , m_gatewayClient(this)
    , m_sceneID(0)
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
    if (!m_server.Start(ip, port)) { LOG_FATAL("SceneServer start failed"); return false; }

    m_superClient.Connect(cfg.superIP, (uint16_t)cfg.superPort);
    if (const ServerEntry* ses = list.findFirst(SubServerType::SESSION))
        m_sessionClient.Connect(ses->ip, ses->port);
    if (const ServerEntry* rec = list.findFirst(SubServerType::RECORD))
        m_recordClient.Connect(rec->ip, rec->port);
    if (const ServerEntry* aoi = list.findFirst(SubServerType::AOI))
        m_aoiClient.Connect(aoi->ip, aoi->port);
    // Gateway 内网口约定：在 ServerList 登记的 gateway 端口基础上 +10000
    if (const ServerEntry* gw = list.findFirst(SubServerType::GATEWAY))
        m_gatewayClient.Connect(gw->ip, (uint16_t)(gw->port + 10000));
    m_listenPort = port;

    SceneManager::Instance().setStartedCallback([this](Scene& scene) { onSceneStarted(scene); });
    SceneManager::Instance().setStoppedCallback([this](Scene& scene) { onSceneStopped(scene); });

    if (!SceneManager::Instance().createNormalScenesFromConfig(m_sceneID, sceneInfo))
        LOG_WARN("Some normal scenes failed to start on SceneServer %u", m_sceneID);

    initMapNpcs();

    if (!LuaManager::Instance().init())
        LOG_WARN("SceneServer Lua init failed");
    RegisterHandlers();

    TimerMgr::Instance().Register(500, 0, [this] { RegisterToSuper(); });
    TimerMgr::Instance().Register(10000, 10000, [this] { SendHeartbeat(); });
    TimerMgr::Instance().Register(1000, 1000, [this] { OnTick(); });

    LOG_INFO("SceneServer %u started on %s:%d", m_sceneID, ip.c_str(), port);
    return true;
}

void SceneServer::Run()
{
    while (true)
    {
        m_superClient.Poll(0);
        m_sessionClient.Poll(0);
        m_recordClient.Poll(0);
        m_aoiClient.Poll(0);
        m_gatewayClient.Poll(0);
        m_server.Poll(10);
        ServerBootstrap::tickGameZoneExtern(m_externHub);
        TimerMgr::Instance().Update();
    }
}

void SceneServer::requestCreateCopy(CopyType copyType, uint32_t mapId, uint64_t ownerId,
                                    const std::string& mapName, const std::string& mapFile,
                                    uint32_t maxPlayer)
{
    Msg_SES_CopyCreateReq req{};
    req.reqSceneServerId = m_sceneID;
    req.copyType = static_cast<uint32_t>(copyType);
    req.mapId = mapId;
    req.ownerId = ownerId;
    req.maxPlayer = maxPlayer;
    copyToWire(req.mapName, sizeof(req.mapName), mapName.c_str());
    copyToWire(req.mapFile, sizeof(req.mapFile), mapFile.c_str());
    m_sessionClient.SendMsg((uint16_t)InternalMsgID::SES_COPY_CREATE_REQ,
                            reinterpret_cast<char*>(&req), sizeof(req));
    LOG_INFO("CopyCreateReq sent: type=%u map=%u owner=%llu", req.copyType, mapId, ownerId);
}

void SceneServer::OnConnect(ConnID /*id*/) {}

void SceneServer::OnDisconnect(ConnID id) { LOG_WARN("SceneServer conn lost=%u", id); }

void SceneServer::OnMessage(ConnID id, uint8_t module, uint8_t sub,
                            const char* data, uint16_t len)
{
    MsgDispatcher::Instance().Dispatch(id, module, sub, data, len);
}

void SceneServer::RegisterHandlers()
{
    auto& d = MsgDispatcher::Instance();
    d.Register((uint16_t)InternalMsgID::SCE_USER_ENTER_REQ,
               [this](uint32_t c, const char* data, uint16_t len) { OnUserEnter(c, data, len); });
    d.Register((uint16_t)InternalMsgID::SCE_USER_LEAVE,
               [this](uint32_t c, const char* data, uint16_t len) { OnUserLeave(c, data, len); });
    d.Register((uint16_t)InternalMsgID::GW_CLIENT_MSG,
               [this](uint32_t c, const char* data, uint16_t len) { OnClientMsg(c, data, len); });
    d.Register((uint16_t)InternalMsgID::AOI_VIEW_NOTIFY,
               [this](uint32_t c, const char* data, uint16_t len) { OnViewNotify(c, data, len); });
    d.Register((uint16_t)InternalMsgID::SES_COPY_CREATE_RSP,
               [this](uint32_t c, const char* data, uint16_t len) { OnCopyCreateRsp(c, data, len); });
    d.Register((uint16_t)InternalMsgID::SES_COPY_CREATE_CMD,
               [this](uint32_t c, const char* data, uint16_t len) { OnCopyCreateCmd(c, data, len); });
}

void SceneServer::OnUserEnter(ConnID /*fromConn*/, const char* data, uint16_t len)
{
    if (len < sizeof(Msg_SCE_UserEnterReq)) return;
    const auto* req = reinterpret_cast<const Msg_SCE_UserEnterReq*>(data);
    LOG_INFO("UserEnter: userID=%llu mapID=%u clientConn=%u",
             req->userID, req->mapID, req->gatewayClientConnID);

    uint32_t mapID = req->mapID ? req->mapID : 1001;
    auto scene = SceneManager::Instance().findNormalSceneByMapId(mapID);
    if (!scene)
    {
        LOG_WARN("Map %u not found on SceneServer %u", mapID, m_sceneID);
        SendUserEnterRsp(req, -1);
        return;
    }

    UserBase base;
    base.userID = req->userID;
    base.name = req->name;
    base.level = req->level;
    base.vocation = req->vocation;
    base.sex = req->sex;
    base.mapID = mapID;
    base.posX = req->x;
    base.posY = req->y;
    base.posZ = req->z;
    base.hp = req->hp;
    base.maxHP = req->maxHP;
    base.mp = req->mp;
    base.maxMP = req->maxMP;
    base.gold = req->gold;

    auto user = SceneUser::create(base);
    user->init();
    user->load();
    user->setGatewayClientConn(req->gatewayClientConnID);
    user->onOnline();
    SceneUserManager::Instance().addUser(req->userID, user);
    scene->addPlayer(req->userID);

    Msg_AOI_Move aoi{};
    aoi.entityID = req->userID;
    aoi.mapID = mapID;
    aoi.x = req->x;
    aoi.y = req->y;
    aoi.z = req->z;
    aoi.entityType = 0;
    m_aoiClient.SendMsg((uint16_t)InternalMsgID::AOI_ENTER_REQ,
                        reinterpret_cast<char*>(&aoi), sizeof(aoi));

    NotifyExistingPlayersOnEnter(*user);
    SendUserEnterRsp(req, 0);
    CallLuaOnEnter(req->userID, mapID);
}

void SceneServer::SendUserEnterRsp(const Msg_SCE_UserEnterReq* req, int32_t code)
{
    Msg_SCE_UserEnterRsp rsp{};
    rsp.code = code;
    rsp.userID = req->userID;
    rsp.gatewayClientConnID = req->gatewayClientConnID;
    rsp.mapID = req->mapID ? req->mapID : 1001;
    m_superClient.SendMsg((uint16_t)InternalMsgID::SCE_USER_ENTER_RSP,
                          reinterpret_cast<char*>(&rsp), sizeof(rsp));
}

void SceneServer::NotifyExistingPlayersOnEnter(const SceneUser& entering)
{
    const auto& base = entering.Base();
    Msg_S2C_SpawnEntity spawn{};
    fillSpawnFromEntry(entering, 0, spawn);

    SceneUserManager::Instance().forEach([&](UserID rid, const std::shared_ptr<SceneUser>& user)
    {
        if (rid == base.userID) return;
        if (user->Base().mapID != base.mapID) return;
        if (user->getGatewayClientConn() == 0) return;

        SendToClient(user->getGatewayClientConn(),
                     (uint16_t)ClientMsgID::S2C_SPAWN_ENTITY,
                     reinterpret_cast<char*>(&spawn), sizeof(spawn));

        Msg_S2C_SpawnEntity other{};
        fillSpawnFromEntry(*user, 0, other);
        SendToClient(entering.getGatewayClientConn(),
                     (uint16_t)ClientMsgID::S2C_SPAWN_ENTITY,
                     reinterpret_cast<char*>(&other), sizeof(other));
    });

    SceneNpcManager::Instance().forEach([&](EntryID /*npcId*/, const std::shared_ptr<SceneNpc>& npc)
    {
        if (!npc || npc->getMapId() != base.mapID) return;
        if (!npc->isAlive()) return;

        Msg_S2C_SpawnEntity npcSpawn{};
        fillSpawnFromEntry(*npc, 1, npcSpawn);
        SendToClient(entering.getGatewayClientConn(),
                     (uint16_t)ClientMsgID::S2C_SPAWN_ENTITY,
                     reinterpret_cast<char*>(&npcSpawn), sizeof(npcSpawn));
    });
}

void SceneServer::OnUserLeave(ConnID /*fromConn*/, const char* data, uint16_t len)
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

    m_aoiClient.SendMsg((uint16_t)InternalMsgID::AOI_LEAVE_REQ,
                        reinterpret_cast<const char*>(&uid), sizeof(uid));
    CallLuaOnLeave(uid);
    SceneUserManager::Instance().removeUser(uid);
    LOG_INFO("UserLeave: userID=%llu", uid);
}

void SceneServer::sendCharBaseToRecord(const SceneUser& user)
{
    Msg_REC_SaveUserReq req{};
    req.userID = user.GetID();
    req.wire = toUserBaseWire(user.Base());
    m_recordClient.SendMsg((uint16_t)InternalMsgID::REC_SAVE_USER_REQ,
                           reinterpret_cast<char*>(&req), sizeof(req));
}

void SceneServer::OnClientMsg(ConnID /*fromConn*/, const char* data, uint16_t len)
{
    if (len < sizeof(Msg_GW_ClientMsg)) return;
    const auto* hdr = reinterpret_cast<const Msg_GW_ClientMsg*>(data);
    const char* body = data + sizeof(Msg_GW_ClientMsg);
    uint16_t bodyLen = hdr->dataLen;
    LOG_DEBUG("ClientMsg: connID=%u mod=0x%02X sub=0x%02X",
              hdr->clientConnID, hdr->module, hdr->sub);
    HandleClientMsg(hdr->clientConnID, hdr->module, hdr->sub, body, bodyLen);
}

void SceneServer::OnViewNotify(ConnID /*fromConn*/, const char* data, uint16_t len)
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

        Msg_S2C_MoveNotify notify{};
        notify.userID = move->entityID;
        notify.x = move->x;
        notify.y = move->y;
        notify.z = move->z;
        notify.dir = move->dir;
        notify.moveType = 0;
        BroadcastToMap(move->mapID, move->entityID,
                       (uint16_t)ClientMsgID::S2C_MOVE_NOTIFY,
                       reinterpret_cast<char*>(&notify), sizeof(notify));
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
        Msg_S2C_SpawnEntity spawn{};
        if (user)
            fillSpawnFromEntry(*user, 0, spawn);
        else
            fillSpawnFromEntry(*npc, 1, spawn);

        BroadcastToMap(mapId, entityID,
                       (uint16_t)ClientMsgID::S2C_SPAWN_ENTITY,
                       reinterpret_cast<char*>(&spawn), sizeof(spawn));
    }
    else
    {
        Msg_S2C_DespawnEntity despawn{};
        despawn.entityID = entityID;
        BroadcastToMap(mapId, entityID,
                       (uint16_t)ClientMsgID::S2C_DESPAWN_ENTITY,
                       reinterpret_cast<char*>(&despawn), sizeof(despawn));
    }
}

void SceneServer::HandleClientMsg(uint32_t clientConnID, uint8_t module, uint8_t sub,
                                  const char* data, uint16_t len)
{
    if (module == static_cast<uint8_t>(ClientModule::SCENE) && sub == 0x01)
        OnMoveReq(clientConnID, data, len);
    else if (module == static_cast<uint8_t>(ClientModule::CHAT) && sub == 0x01)
        OnChatReq(clientConnID, data, len);
    else if (module == static_cast<uint8_t>(ClientModule::SKILL) && sub == 0x01)
        OnSkillReq(clientConnID, data, len);
    else if (module == static_cast<uint8_t>(ClientModule::NPC) && sub == 0x01)
        OnNpcTalkReq(clientConnID, data, len);
    else if (module == static_cast<uint8_t>(ClientModule::SYSTEM) && sub == 0x01)
        OnHeartbeatReq(clientConnID, data, len);
    else
        CallLuaMsgHandler(clientConnID, module, sub, data, len);
}

void SceneServer::OnMoveReq(uint32_t /*clientConnID*/, const char* data, uint16_t len)
{
    if (len < sizeof(Msg_C2S_MoveReq)) return;
    const auto* req = reinterpret_cast<const Msg_C2S_MoveReq*>(data);
    auto user = SceneUserManager::Instance().findUser(req->userID);
    if (!user) return;
    user->Base().posX = req->x;
    user->Base().posY = req->y;
    user->Base().posZ = req->z;
    user->markDirty();

    Msg_AOI_Move aoi{};
    aoi.entityID = req->userID;
    aoi.mapID = user->Base().mapID;
    aoi.x = req->x; aoi.y = req->y; aoi.z = req->z; aoi.dir = req->dir;
    aoi.entityType = 0;
    m_aoiClient.SendMsg((uint16_t)InternalMsgID::AOI_MOVE_REQ,
                        reinterpret_cast<char*>(&aoi), sizeof(aoi));
}

void SceneServer::OnChatReq(uint32_t clientConnID, const char* data, uint16_t len)
{
    if (len < sizeof(Msg_C2S_Chat)) return;
    const auto* req = reinterpret_cast<const Msg_C2S_Chat*>(data);
    auto user = SceneUserManager::Instance().findUserByClientConn(clientConnID);
    if (!user) return;

    Msg_S2C_Chat notify{};
    notify.fromID = user->GetID();
    notify.channel = req->channel;
    snprintf(notify.fromName, sizeof(notify.fromName), "%s", user->GetName());
    snprintf(notify.content, sizeof(notify.content), "%s", req->content);
    BroadcastToMap(user->Base().mapID, user->GetID(),
                   (uint16_t)ClientMsgID::S2C_CHAT_NOTIFY,
                   reinterpret_cast<char*>(&notify), sizeof(notify));
}

void SceneServer::OnSkillReq(uint32_t clientConnID, const char* data, uint16_t len)
{
    LOG_DEBUG("SkillReq from conn=%u", clientConnID);
    CallLuaSkillHandler(clientConnID, data, len);
}

void SceneServer::OnNpcTalkReq(uint32_t clientConnID, const char* data, uint16_t len)
{
    if (len < sizeof(Msg_C2S_NpcTalkReq))
        return;

    const auto* req = reinterpret_cast<const Msg_C2S_NpcTalkReq*>(data);
    auto user = SceneUserManager::Instance().findUserByClientConn(clientConnID);
    if (!user || user->getGatewayClientConn() == 0)
        return;

    auto npc = SceneNpcManager::Instance().findNpc(req->npcId);
    if (!npc || npc->isDead())
    {
        sendNpcTalkError(user->getGatewayClientConn(), req->npcId, 1);
        return;
    }

    if (npc->getMapId() != user->Base().mapID)
    {
        sendNpcTalkError(user->getGatewayClientConn(), req->npcId, 2);
        return;
    }

    const bool ok = LuaManager::Instance().callScriptBool(npc.get(), "OnNpcTalk", {
        LuaArg::integer(static_cast<int64_t>(user->GetID())),
        LuaArg::integer(req->dialogStep),
        LuaArg::integer(npc->getTemplateId()),
    });

    if (!ok)
        sendNpcTalkError(user->getGatewayClientConn(), req->npcId, 3);
}

void SceneServer::sendNpcTalkError(uint32_t clientConnID, uint64_t npcId, int32_t code)
{
    Msg_S2C_NpcTalkRsp rsp{};
    rsp.code = code;
    rsp.npcId = npcId;
    rsp.dialogStep = 0;
    rsp.optionCount = 0;
    SendToClient(clientConnID, (uint16_t)ClientMsgID::S2C_NPC_TALK_RSP,
                 reinterpret_cast<char*>(&rsp), sizeof(rsp));
}

void SceneServer::OnHeartbeatReq(uint32_t clientConnID, const char* data, uint16_t len)
{
    Msg_S2C_Heartbeat rsp{};
    if (len >= sizeof(Msg_C2S_Heartbeat))
        rsp.seq = reinterpret_cast<const Msg_C2S_Heartbeat*>(data)->seq;
    rsp.serverTime = TimerMgr::NowMs();
    SendToClient(clientConnID, (uint16_t)ClientMsgID::S2C_HEARTBEAT,
                 reinterpret_cast<char*>(&rsp), sizeof(rsp));
}

void SceneServer::SendToClient(uint32_t clientConnID, uint8_t module, uint8_t sub,
                               const char* data, uint16_t len)
{
    std::vector<char> buf(sizeof(Msg_GW_SendToClient) + len);
    auto* hdr = reinterpret_cast<Msg_GW_SendToClient*>(buf.data());
    hdr->clientConnID = clientConnID;
    hdr->module = module;
    hdr->sub = sub;
    hdr->dataLen = len;
    if (len > 0)
        memcpy(buf.data() + sizeof(Msg_GW_SendToClient), data, len);
    m_gatewayClient.SendMsg(static_cast<uint16_t>(InternalMsgID::GW_SEND_TO_CLIENT),
                            buf.data(), static_cast<uint16_t>(buf.size()));
}

void SceneServer::SendToClient(uint32_t clientConnID, uint16_t flatMsgId,
                               const char* data, uint16_t len)
{
    SendToClient(clientConnID,
                 static_cast<uint8_t>(flatMsgId >> 8),
                 static_cast<uint8_t>(flatMsgId & 0xFF),
                 data, len);
}

void SceneServer::BroadcastToMap(uint32_t mapID, UserID excludeUserID,
                                 uint16_t msgID, const char* data, uint16_t len)
{
    SceneUserManager::Instance().forEach([&](UserID uid, const std::shared_ptr<SceneUser>& user)
    {
        if (mapID != 0 && user->Base().mapID != mapID) return;
        if (uid == excludeUserID) return;
        if (user->getGatewayClientConn() == 0) return;
        SendToClient(user->getGatewayClientConn(), msgID, data, len);
    });
}

void SceneServer::CallLuaOnEnter(UserID userID, uint32_t mapID)
{
    LuaManager::Instance().callGlobalVoid("OnUserEnter", {
        LuaArg::integer(static_cast<int64_t>(userID)),
        LuaArg::integer(mapID),
    });
}

void SceneServer::CallLuaOnLeave(UserID userID)
{
    LuaManager::Instance().callGlobalVoid("OnUserLeave", {
        LuaArg::integer(static_cast<int64_t>(userID)),
    });
}

void SceneServer::CallLuaMsgHandler(uint32_t connID, uint8_t module, uint8_t sub,
                                    const char* data, uint16_t len)
{
    char funcName[32];
    snprintf(funcName, sizeof(funcName), "OnMsg_%02X%02X", module, sub);
    LuaManager::Instance().callGlobalVoid(funcName, {
        LuaArg::integer(connID),
        LuaArg::binary(data, len),
    });
}

void SceneServer::CallLuaSkillHandler(uint32_t connID, const char* data, uint16_t len)
{
    LuaManager::Instance().callGlobalVoid("OnSkillReq", {
        LuaArg::integer(connID),
        LuaArg::binary(data, len),
    });
}

void SceneServer::OnTick()
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
            LOG_INFO("NPC spawned: id=%llu map=%u", def.npcId, mapId);
            auto npc = SceneNpcManager::Instance().findNpc(def.npcId);
            if (npc)
                notifyNpcEnterAoi(*npc);
        }
    });
}

void SceneServer::onSceneStarted(Scene& scene)
{
    Msg_AOI_SceneRegister aoiReg{};
    aoiReg.sceneServerId = m_sceneID;
    aoiReg.sceneInstanceId = scene.getSceneInstanceId();
    aoiReg.mapId = scene.getMapId();
    aoiReg.sceneKind = static_cast<uint8_t>(scene.getSceneKind());
    aoiReg.maxPlayer = scene.getMaxPlayer();
    m_aoiClient.SendMsg((uint16_t)InternalMsgID::AOI_SCENE_REGISTER,
                        reinterpret_cast<char*>(&aoiReg), sizeof(aoiReg));

    Msg_SES_SceneRegisterReq sesReg{};
    sesReg.sceneServerId = m_sceneID;
    sesReg.sceneInstanceId = scene.getSceneInstanceId();
    sesReg.mapId = scene.getMapId();
    sesReg.sceneKind = static_cast<uint8_t>(scene.getSceneKind());
    sesReg.maxPlayer = scene.getMaxPlayer();
    copyToWire(sesReg.mapName, sizeof(sesReg.mapName), scene.getMapName().c_str());
    copyToWire(sesReg.mapFile, sizeof(sesReg.mapFile), scene.getMapFile().c_str());
    m_sessionClient.SendMsg((uint16_t)InternalMsgID::SES_SCENE_REGISTER_REQ,
                            reinterpret_cast<char*>(&sesReg), sizeof(sesReg));

    LOG_INFO("Scene registered AOI+Session: instance=%llu map=%u kind=%u",
             scene.getSceneInstanceId(), scene.getMapId(),
             static_cast<unsigned>(scene.getSceneKind()));
}

void SceneServer::onSceneStopped(Scene& scene)
{
    Msg_AOI_SceneUnregister aoiUnreg{};
    aoiUnreg.sceneInstanceId = scene.getSceneInstanceId();
    m_aoiClient.SendMsg((uint16_t)InternalMsgID::AOI_SCENE_UNREGISTER,
                        reinterpret_cast<char*>(&aoiUnreg), sizeof(aoiUnreg));

    Msg_SES_SceneUnregister sesUnreg{};
    sesUnreg.sceneInstanceId = scene.getSceneInstanceId();
    sesUnreg.sceneServerId = m_sceneID;
    m_sessionClient.SendMsg((uint16_t)InternalMsgID::SES_SCENE_UNREGISTER,
                            reinterpret_cast<char*>(&sesUnreg), sizeof(sesUnreg));
}

void SceneServer::OnCopyCreateRsp(ConnID /*fromConn*/, const char* data, uint16_t len)
{
    if (len < sizeof(Msg_SES_CopyCreateRsp)) return;
    const auto* rsp = reinterpret_cast<const Msg_SES_CopyCreateRsp*>(data);
    LOG_INFO("CopyCreateRsp: code=%d targetServer=%u instance=%llu reused=%u",
             rsp->code, rsp->targetSceneServerId, rsp->copyInstanceId, rsp->reused);
}

void SceneServer::OnCopyCreateCmd(ConnID /*fromConn*/, const char* data, uint16_t len)
{
    if (len < sizeof(Msg_SES_CopyCreateCmd)) return;
    const auto* cmd = reinterpret_cast<const Msg_SES_CopyCreateCmd*>(data);

    CopySceneDef def{};
    def.copyInstanceId = cmd->copyInstanceId;
    def.copyType = static_cast<CopyType>(cmd->copyType);
    def.mapId = cmd->mapId;
    def.ownerId = cmd->ownerId;
    def.maxPlayer = cmd->maxPlayer;
    def.mapName = cmd->mapName;
    def.mapFile = cmd->mapFile;

    if (SceneManager::Instance().createCopyScene(m_sceneID, def))
        LOG_INFO("CopyScene created locally: instance=%llu", cmd->copyInstanceId);
    else
        LOG_ERR("CopyScene create failed: instance=%llu", cmd->copyInstanceId);
}

void SceneServer::fillSpawnFromEntry(const SceneEntry& entry, uint8_t entityType,
                                     Msg_S2C_SpawnEntity& spawn)
{
    spawn.entityID = entry.getEntryId();
    spawn.level = entry.getLevel();
    spawn.x = entry.getPosX();
    spawn.y = entry.getPosY();
    spawn.z = entry.getPosZ();
    spawn.dir = 0.f;
    spawn.entityType = entityType;
    copyToWire(spawn.name, sizeof(spawn.name), entry.getName().c_str());
}

void SceneServer::sendAoiEnter(const SceneEntry& entry, uint8_t entityType)
{
    Msg_AOI_Move aoi{};
    aoi.entityID = entry.getEntryId();
    aoi.mapID = entry.getMapId();
    aoi.x = entry.getPosX();
    aoi.y = entry.getPosY();
    aoi.z = entry.getPosZ();
    aoi.dir = 0.f;
    aoi.entityType = entityType;
    m_aoiClient.SendMsg((uint16_t)InternalMsgID::AOI_ENTER_REQ,
                        reinterpret_cast<char*>(&aoi), sizeof(aoi));
}

void SceneServer::sendAoiLeave(EntryID entityId)
{
    m_aoiClient.SendMsg((uint16_t)InternalMsgID::AOI_LEAVE_REQ,
                        reinterpret_cast<const char*>(&entityId), sizeof(entityId));
}

void SceneServer::setupExternalClients(const LoginServerList& list)
{
    ServerBootstrap::initGameZoneExtern(m_externHub, list, SubServerType::SCENE, true, true);
}

void SceneServer::RegisterToSuper()
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

void SceneServer::SendHeartbeat()
{
    Msg_S2S_Heartbeat hb{};
    hb.seq = ++m_hbSeq;
    hb.timestamp = TimerMgr::NowMs();
    m_superClient.SendMsg((uint16_t)InternalMsgID::S2S_HEARTBEAT,
                          reinterpret_cast<char*>(&hb), sizeof(hb));
}
