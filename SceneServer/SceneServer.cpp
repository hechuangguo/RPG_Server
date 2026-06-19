/**
 * @file SceneServer.cpp
 * @brief SceneServer 非简单成员函数实现，降低头文件编译耦合。
 */

#include "SceneServer.h"
#include "SceneInternMsgRegister.h"
#include "SceneClientMsgRegister.h"
#include "../sdk/net/MsgIngress.h"
#include "../sdk/net/NetTls.h"
#include "ScenePeerClient.h"
#include "../sdk/util/ServerBootstrap.h"
#include "../sdk/util/GameZoneExternSender.h"
#include "SceneLoginMsg.h"
#include "SceneUserManager.h"
#include "SceneNpcManager.h"
#include "SceneManager.h"
#include "LuaManager.h"

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

    LOG_INFO("场景服启动完成: sceneID=%u %s:%d", m_sceneID, ip.c_str(), port);
    return true;
}

void SceneServer::Run()
{
    while (true)
    {
        m_superClient.Poll(0);
        m_sessionClient.poll();
        m_recordClient.poll();
        m_aoiClient.poll();
        m_server.Poll(10);
        TimerMgr::Instance().Update();
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

    uint32_t mapID = req.mapID ? req.mapID : 1001;
    auto scene = SceneManager::Instance().findNormalSceneByMapId(mapID);
    if (!scene)
    {
        LOG_WARN("场景服未找到地图: map=%u sceneID=%u", mapID, m_sceneID);
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
    Msg_S2C_SpawnEntity spawn{};
    fillSpawnFromEntry(entering, 0, spawn);

    SceneUserManager::Instance().forEach([&](UserID rid, const std::shared_ptr<SceneUser>& user)
    {
        if (rid == base.userID) return;
        if (user->Base().mapID != base.mapID) return;
        if (user->getGatewayClientConn() == 0) return;

        SendToClient(user->getGatewayClientConn(),
                     Msg_S2C_SpawnEntity::kModule, Msg_S2C_SpawnEntity::kSub,
                     reinterpret_cast<char*>(&spawn), sizeof(spawn));

        Msg_S2C_SpawnEntity other{};
        fillSpawnFromEntry(*user, 0, other);
        SendToClient(entering.getGatewayClientConn(),
                     Msg_S2C_SpawnEntity::kModule, Msg_S2C_SpawnEntity::kSub,
                     reinterpret_cast<char*>(&other), sizeof(other));
    });

    SceneNpcManager::Instance().forEach([&](EntryID /*npcId*/, const std::shared_ptr<SceneNpc>& npc)
    {
        if (!npc || npc->getMapId() != base.mapID) return;
        if (!npc->isAlive()) return;

        Msg_S2C_SpawnEntity npcSpawn{};
        fillSpawnFromEntry(*npc, 1, npcSpawn);
        SendToClient(entering.getGatewayClientConn(),
                     Msg_S2C_SpawnEntity::kModule, Msg_S2C_SpawnEntity::kSub,
                     reinterpret_cast<char*>(&npcSpawn), sizeof(npcSpawn));
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

        Msg_S2C_MoveNotify notify{};
        notify.userID = move->entityID;
        notify.x = move->x;
        notify.y = move->y;
        notify.z = move->z;
        notify.dir = move->dir;
        notify.moveType = 0;
        BroadcastToMap(move->mapID, move->entityID,
                       Msg_S2C_MoveNotify::kModule, Msg_S2C_MoveNotify::kSub,
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
                       Msg_S2C_SpawnEntity::kModule, Msg_S2C_SpawnEntity::kSub,
                       reinterpret_cast<char*>(&spawn), sizeof(spawn));
    }
    else
    {
        Msg_S2C_DespawnEntity despawn{};
        despawn.entityID = entityID;
        BroadcastToMap(mapId, entityID,
                       Msg_S2C_DespawnEntity::kModule, Msg_S2C_DespawnEntity::kSub,
                       reinterpret_cast<char*>(&despawn), sizeof(despawn));
    }
}

void SceneServer::onMoveReq(uint32_t /*clientConnID*/, const char* data, uint16_t len)
{
    if (len < sizeof(Msg_C2S_MoveReq)) return;
    const auto* req = reinterpret_cast<const Msg_C2S_MoveReq*>(data);
    auto user = SceneUserManager::Instance().findUser(req->userID);
    if (!user) return;
    user->Base().posX = req->x;
    user->Base().posY = req->y;
    user->Base().posZ = req->z;
    user->markDirty();

    m_aoiClient.moveEntity(req->userID, user->Base().mapID, req->x, req->y, req->z,
                             req->dir, 0);
}

void SceneServer::onChatReq(uint32_t clientConnID, const char* data, uint16_t len)
{
    if (len < sizeof(Msg_C2S_Chat)) return;
    const auto* req = reinterpret_cast<const Msg_C2S_Chat*>(data);
    auto user = SceneUserManager::Instance().findUserByClientConn(clientConnID);
    if (!user) return;

    Msg_S2C_Chat notify{};
    notify.fromID = user->GetID();
    notify.channel = req->channel;
    copyToWire(notify.fromName, sizeof(notify.fromName), user->GetName());
    copyToWire(notify.content, sizeof(notify.content), req->content);
    BroadcastToMap(user->Base().mapID, user->GetID(),
                   Msg_S2C_Chat::kModule, Msg_S2C_Chat::kSub,
                   reinterpret_cast<char*>(&notify), sizeof(notify));
}

void SceneServer::onNpcTalkReq(uint32_t clientConnID, const char* data, uint16_t len)
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
    initClientMsg(rsp);
    rsp.code = code;
    rsp.npcId = npcId;
    rsp.dialogStep = 0;
    rsp.optionCount = 0;
    SendToClient(clientConnID, Msg_S2C_NpcTalkRsp::kModule, Msg_S2C_NpcTalkRsp::kSub,
                 reinterpret_cast<char*>(&rsp), sizeof(rsp));
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
    Msg_S2S_Heartbeat hb{};
    hb.seq = ++m_hbSeq;
    hb.timestamp = TimerMgr::NowMs();
    m_superClient.SendMsg((uint16_t)InternalMsgID::S2S_HEARTBEAT,
                          reinterpret_cast<char*>(&hb), sizeof(hb));
}
