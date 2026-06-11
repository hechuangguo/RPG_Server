/**
 * @file    SessionServer.cpp
 * @brief   SessionServer 核心流程与消息处理实现
 */

#include "SessionServer.h"
#include "../sdk/util/ServerBootstrap.h"
#include "SessionUserManager.h"
#include "SessionSceneManager.h"

namespace {

constexpr uint64_t RELATION_SYNC_TIMEOUT_MS = 30000;

} // namespace

SessionServer::SessionServer()
    : m_server(this)
    , m_superClient(this)
    , m_recordClient(this)
{
}

SessionServer::~SessionServer() = default;

bool SessionServer::Init(const std::string& ip, uint16_t port,
                         const ServerConfig& cfg, const ServerList& list, uint32_t selfId)
{
    Logger::Instance().SetServerName("SessionServer");
    LOG_INFO("SessionServer starting on %s:%d", ip.c_str(), port);
    if (const ServerEntry* self = list.find(SubServerType::SESSION, selfId))
        m_self = *self;
    if (!m_server.Start(ip, port))
    {
        LOG_FATAL("Start failed");
        return false;
    }
    if (!m_superClient.Connect(cfg.superIP, (uint16_t)cfg.superPort))
        LOG_WARN("Cannot connect to SuperServer");

    const ServerEntry* rec = list.findFirst(SubServerType::RECORD);
    if (!rec)
    {
        LOG_FATAL("ServerList missing RECORD entry");
        return false;
    }
    if (!m_recordClient.Connect(rec->ip, rec->port))
    {
        LOG_FATAL("Cannot connect to RecordServer at %s:%u", rec->ip.c_str(), rec->port);
        return false;
    }

    RegisterHandlers();

    const uint64_t connectDeadline = TimerMgr::NowMs() + 5000;
    while (!m_recordClient.IsConnected() && TimerMgr::NowMs() < connectDeadline)
        pollForRelationSync();

    if (!m_recordClient.IsConnected())
    {
        LOG_FATAL("RecordServer connection not ready (start Record before Session)");
        return false;
    }

    if (!preloadRelations())
    {
        LOG_FATAL("Relation preload from RecordServer failed");
        return false;
    }

    TimerMgr::Instance().Register(500, 0, [this] { RegisterToSuper(); });
    TimerMgr::Instance().Register(10000, 10000, [this] { SendHeartbeat(); });
    TimerMgr::Instance().Register(60000, 60000, [this] { AutoSaveAll(); });
    LOG_INFO("SessionServer started (Record + SceneManager).");
    return true;
}

void SessionServer::Run()
{
    while (true)
    {
        m_superClient.Poll(0);
        m_recordClient.Poll(0);
        m_server.Poll(10);
        ServerBootstrap::tickGameZoneExtern(m_externHub);
        TimerMgr::Instance().Update();
    }
}

void SessionServer::setupExternalClients(const LoginServerList& list)
{
    ServerBootstrap::initGameZoneExtern(m_externHub, list, SubServerType::SESSION, false, true);
}

void SessionServer::OnConnect(ConnID id)
{
    LOG_INFO("InnerConn connected=%u", id);
}

void SessionServer::OnDisconnect(ConnID id)
{
    SessionSceneManager::Instance().unbindConn(id);
    LOG_INFO("InnerConn disconnected=%u", id);
}

void SessionServer::OnMessage(ConnID id, uint8_t module, uint8_t sub, const char* data, uint16_t len)
{
    MsgDispatcher::Instance().Dispatch(id, module, sub, data, len);
}

void SessionServer::pollForRelationSync()
{
    m_recordClient.Poll(10);
    m_superClient.Poll(0);
    m_server.Poll(0);
}

bool SessionServer::preloadRelations()
{
    m_relationPreloadDone = false;
    m_relationPreloadOk   = false;

    Msg_REC_RelationPreloadReq req{};
    if (!m_recordClient.SendMsg((uint16_t)InternalMsgID::REC_RELATION_PRELOAD_REQ,
                                reinterpret_cast<char*>(&req), sizeof(req)))
    {
        LOG_ERR("Failed to send REC_RELATION_PRELOAD_REQ");
        return false;
    }

    const uint64_t deadline = TimerMgr::NowMs() + RELATION_SYNC_TIMEOUT_MS;
    while (!m_relationPreloadDone && TimerMgr::NowMs() < deadline)
        pollForRelationSync();

    return m_relationPreloadDone && m_relationPreloadOk;
}

bool SessionServer::loadRelationSync(UserID userID, RelationRowData& out)
{
    m_relationLoadDone = false;
    m_relationLoadOk   = false;

    if (!m_recordClient.SendMsg((uint16_t)InternalMsgID::REC_RELATION_LOAD_REQ,
                                reinterpret_cast<const char*>(&userID), sizeof(userID)))
        return false;

    const uint64_t deadline = TimerMgr::NowMs() + RELATION_SYNC_TIMEOUT_MS;
    while (!m_relationLoadDone && TimerMgr::NowMs() < deadline)
        pollForRelationSync();

    if (!m_relationLoadOk)
        return false;
    out = m_relationLoadRow;
    return true;
}

bool SessionServer::saveRelation(const RelationRowData& row)
{
    std::vector<char> buf;
    RelationWireUtil::appendRow(row, buf);
    return m_recordClient.SendMsg((uint16_t)InternalMsgID::REC_RELATION_SAVE_REQ,
                                  buf.data(), static_cast<uint16_t>(buf.size()));
}

void SessionServer::RegisterHandlers()
{
    auto& d = MsgDispatcher::Instance();
    d.Register((uint16_t)InternalMsgID::S2S_HEARTBEAT_ACK, [](uint32_t, const char*, uint16_t) {});
    d.Register((uint16_t)InternalMsgID::REC_RELATION_PRELOAD_RSP,
               [this](uint32_t c, const char* d, uint16_t l) { OnRelationPreloadRsp(c, d, l); });
    d.Register((uint16_t)InternalMsgID::REC_RELATION_LOAD_RSP,
               [this](uint32_t c, const char* d, uint16_t l) { OnRelationLoadRsp(c, d, l); });
    d.Register((uint16_t)InternalMsgID::SES_LOAD_USER_REQ,
               [this](uint32_t c, const char* d, uint16_t l) { OnLoadUserReq(c, d, l); });
    d.Register((uint16_t)InternalMsgID::SES_SAVE_USER_REQ,
               [this](uint32_t c, const char* d, uint16_t l) { OnSaveUserReq(c, d, l); });
    d.Register((uint16_t)InternalMsgID::SES_FRIEND_UPDATE,
               [this](uint32_t c, const char* d, uint16_t l) { OnFriendUpdate(c, d, l); });
    d.Register((uint16_t)InternalMsgID::SES_SCENE_REGISTER_REQ,
               [this](uint32_t c, const char* d, uint16_t l) { OnSceneRegisterReq(c, d, l); });
    d.Register((uint16_t)InternalMsgID::SES_SCENE_UNREGISTER,
               [this](uint32_t c, const char* d, uint16_t l) { OnSceneUnregister(c, d, l); });
    d.Register((uint16_t)InternalMsgID::SES_COPY_CREATE_REQ,
               [this](uint32_t c, const char* d, uint16_t l) { OnCopyCreateReq(c, d, l); });
    d.Register((uint16_t)InternalMsgID::GW_CLIENT_MSG,
               [this](uint32_t c, const char* d, uint16_t l) { OnGatewayClientMsg(c, d, l); });
}

void SessionServer::OnRelationPreloadRsp(ConnID /*fromConn*/, const char* data, uint16_t len)
{
    if (len < sizeof(Msg_REC_RelationPreloadRsp))
    {
        m_relationPreloadOk   = false;
        m_relationPreloadDone = true;
        return;
    }
    const auto* hdr = reinterpret_cast<const Msg_REC_RelationPreloadRsp*>(data);
    if (hdr->code != 0)
    {
        LOG_ERR("Relation preload rsp code=%d", hdr->code);
        m_relationPreloadOk   = false;
        m_relationPreloadDone = true;
        return;
    }

    std::vector<RelationRowData> rows;
    if (!RelationWireUtil::parseAllRows(data, len, sizeof(Msg_REC_RelationPreloadRsp), rows))
    {
        LOG_ERR("Relation preload rsp parse failed");
        m_relationPreloadOk   = false;
        m_relationPreloadDone = true;
        return;
    }

    SessionUserManager::Instance().applyPreloadRows(rows);
    m_relationPreloadOk   = true;
    m_relationPreloadDone = true;
}

void SessionServer::OnRelationLoadRsp(ConnID /*fromConn*/, const char* data, uint16_t len)
{
    if (len < sizeof(Msg_REC_RelationLoadRsp))
    {
        m_relationLoadOk   = false;
        m_relationLoadDone = true;
        return;
    }
    const auto* hdr = reinterpret_cast<const Msg_REC_RelationLoadRsp*>(data);
    if (hdr->code != 0)
    {
        m_relationLoadOk   = false;
        m_relationLoadDone = true;
        return;
    }

    std::vector<RelationRowData> rows;
    if (!RelationWireUtil::parseAllRows(data, len, sizeof(Msg_REC_RelationLoadRsp), rows)
        || rows.empty())
    {
        m_relationLoadOk   = false;
        m_relationLoadDone = true;
        return;
    }

    m_relationLoadRow  = rows[0];
    m_relationLoadOk   = true;
    m_relationLoadDone = true;
}

void SessionServer::OnGatewayClientMsg(ConnID /*fromConn*/, const char* data, uint16_t len)
{
    if (len < sizeof(Msg_GW_ClientMsg))
        return;
    const auto* hdr = reinterpret_cast<const Msg_GW_ClientMsg*>(data);
    const char* body = data + sizeof(Msg_GW_ClientMsg);
    if (len < sizeof(Msg_GW_ClientMsg) + hdr->dataLen)
        return;

    LOG_INFO("GatewayClientMsg: conn=%u mod=0x%02X sub=0x%02X len=%u",
             hdr->clientConnID, hdr->module, hdr->sub, hdr->dataLen);

    if (hdr->module == static_cast<uint8_t>(ClientModule::SOCIAL))
        handleSocialClientMsg(hdr->clientConnID, hdr->sub, body, hdr->dataLen);
    else if (hdr->module == static_cast<uint8_t>(ClientModule::QUEST))
        handleQuestClientMsg(hdr->clientConnID, hdr->sub, body, hdr->dataLen);
}

void SessionServer::handleSocialClientMsg(uint32_t clientConnId, uint8_t sub,
                                          const char* /*data*/, uint16_t len)
{
    LOG_DEBUG("SocialClientMsg: conn=%u sub=0x%02X len=%u", clientConnId, sub, len);
}

void SessionServer::handleQuestClientMsg(uint32_t clientConnId, uint8_t sub,
                                       const char* /*data*/, uint16_t len)
{
    LOG_DEBUG("QuestClientMsg: conn=%u sub=0x%02X len=%u", clientConnId, sub, len);
}

void SessionServer::RegisterToSuper()
{
    Msg_S2S_Register reg{};
    reg.serverType = (uint8_t)SubServerType::SESSION;
    reg.serverID = m_self.id;
    copyToWire(reg.ip, sizeof(reg.ip), m_self.ip.c_str());
    reg.port = m_self.port;
    copyToWire(reg.name, sizeof(reg.name), m_self.name.c_str());
    m_superClient.SendMsg((uint16_t)InternalMsgID::S2S_REGISTER_REQ,
                          reinterpret_cast<char*>(&reg), sizeof(reg));
}

void SessionServer::SendHeartbeat()
{
    Msg_S2S_Heartbeat hb{};
    hb.seq = ++m_hbSeq;
    hb.timestamp = TimerMgr::NowMs();
    m_superClient.SendMsg((uint16_t)InternalMsgID::S2S_HEARTBEAT,
                          reinterpret_cast<char*>(&hb), sizeof(hb));
}

void SessionServer::OnLoadUserReq(ConnID fromConn, const char* data, uint16_t len)
{
    if (len < sizeof(UserID))
        return;
    UserID uid = *reinterpret_cast<const UserID*>(data);

    auto user = SessionUserManager::Instance().getOrCreateUser(uid);
    user->load(*this);
    user->onOnline();

    m_server.SendMsg(fromConn, (uint16_t)InternalMsgID::SES_LOAD_USER_RSP, data, len);
}

void SessionServer::OnSaveUserReq(ConnID /*fromConn*/, const char* data, uint16_t len)
{
    if (len < sizeof(UserID))
        return;
    UserID uid = *reinterpret_cast<const UserID*>(data);
    auto user = SessionUserManager::Instance().findUser(uid);
    if (!user)
        user = SessionUserManager::Instance().getOrCreateUser(uid);
    user->save(*this);
}

void SessionServer::OnFriendUpdate(ConnID /*fromConn*/, const char* /*data*/, uint16_t len)
{
    LOG_DEBUG("FriendUpdate len=%d", len);
}

void SessionServer::OnSceneRegisterReq(ConnID fromConn, const char* data, uint16_t len)
{
    if (len < sizeof(Msg_SES_SceneRegisterReq))
        return;
    const auto* req = reinterpret_cast<const Msg_SES_SceneRegisterReq*>(data);

    SessionSceneManager::Instance().registerScene(fromConn, *req);

    Msg_SES_SceneRegisterRsp rsp{};
    rsp.code = 0;
    rsp.sceneInstanceId = req->sceneInstanceId;
    m_server.SendMsg(fromConn, (uint16_t)InternalMsgID::SES_SCENE_REGISTER_RSP,
                     reinterpret_cast<char*>(&rsp), sizeof(rsp));
}

void SessionServer::OnSceneUnregister(ConnID /*fromConn*/, const char* data, uint16_t len)
{
    if (len < sizeof(Msg_SES_SceneUnregister))
        return;
    const auto* req = reinterpret_cast<const Msg_SES_SceneUnregister*>(data);
    SessionSceneManager::Instance().unregisterScene(req->sceneInstanceId, req->sceneServerId);
}

void SessionServer::OnCopyCreateReq(ConnID fromConn, const char* data, uint16_t len)
{
    if (len < sizeof(Msg_SES_CopyCreateReq))
        return;
    const auto* req = reinterpret_cast<const Msg_SES_CopyCreateReq*>(data);

    SessionSceneManager::Instance().bindSceneServer(fromConn, req->reqSceneServerId);

    Msg_SES_CopyCreateRsp rsp{};
    rsp.code = 0;

    const CopyType copyType = static_cast<CopyType>(req->copyType);
    SessionCopyScene* existing =
        SessionSceneManager::Instance().findReusableCopy(copyType, req->mapId, req->ownerId);

    if (existing)
    {
        rsp.targetSceneServerId = existing->getSceneServerId();
        rsp.copyInstanceId = existing->getCopyInstanceId();
        rsp.copyType = req->copyType;
        rsp.mapId = req->mapId;
        rsp.ownerId = req->ownerId;
        rsp.maxPlayer = existing->getMaxPlayer();
        rsp.reused = 1;
        copyWireField(rsp.mapName, req->mapName);
        copyWireField(rsp.mapFile, req->mapFile);
    }
    else
    {
        uint32_t targetId = SessionSceneManager::Instance().pickSceneServerId();
        if (targetId == 0)
            targetId = req->reqSceneServerId;

        const uint64_t copyId = SessionSceneManager::Instance().generateCopyInstanceId();
        SessionSceneManager::Instance().createCopyRecord(targetId, copyId, *req);

        rsp.targetSceneServerId = targetId;
        rsp.copyInstanceId = copyId;
        rsp.copyType = req->copyType;
        rsp.mapId = req->mapId;
        rsp.ownerId = req->ownerId;
        rsp.maxPlayer = req->maxPlayer;
        rsp.reused = 0;
        copyWireField(rsp.mapName, req->mapName);
        copyWireField(rsp.mapFile, req->mapFile);

        Msg_SES_CopyCreateCmd cmd{};
        cmd.copyInstanceId = copyId;
        cmd.copyType = req->copyType;
        cmd.mapId = req->mapId;
        cmd.ownerId = req->ownerId;
        cmd.maxPlayer = req->maxPlayer;
        copyWireField(cmd.mapName, req->mapName);
        copyWireField(cmd.mapFile, req->mapFile);

        ConnID targetConn = SessionSceneManager::Instance().findConnBySceneServerId(targetId);
        if (targetConn != INVALID_CONN_ID)
        {
            m_server.SendMsg(targetConn, (uint16_t)InternalMsgID::SES_COPY_CREATE_CMD,
                             reinterpret_cast<char*>(&cmd), sizeof(cmd));
        }
        else
        {
            LOG_WARN("CopyCreateCmd: target SceneServer %u not connected", targetId);
            rsp.code = -1;
        }
    }

    m_server.SendMsg(fromConn, (uint16_t)InternalMsgID::SES_COPY_CREATE_RSP,
                     reinterpret_cast<char*>(&rsp), sizeof(rsp));
}

void SessionServer::AutoSaveAll()
{
    SessionUserManager::Instance().forEach([this](UserID uid, const std::shared_ptr<SessionUser>& user) {
        (void)uid;
        if (user->needSave())
            user->save(*this);
    });
}
