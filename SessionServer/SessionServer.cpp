/**
 * @file    SessionServer.cpp
 * @brief   SessionServer 核心流程与消息处理实现
 */

#include "SessionServer.h"
#include "SessionInternMsgRegister.h"
#include "SessionClientMsgRegister.h"
#include "../sdk/net/MsgIngress.h"
#include "../sdk/net/NetTls.h"
#include "../sdk/util/ServerBootstrap.h"
#include "SessionUserManager.h"
#include "SessionSceneManager.h"
#include "SessionLoginMsg.h"
#include "../sdk/net/GwClientRelay.h"
#include "../sdk/util/LoginFlowLog.h"
#include "../sdk/util/ServiceHealthMetrics.h"
#include "../sdk/util/LoginFlowTimeouts.h"

#include <vector>
#include <cstdio>

namespace {

constexpr uint64_t RELATION_PRELOAD_TIMEOUT_MS = 30000;
constexpr uint64_t RELATION_ASYNC_LOAD_TIMEOUT_MS = VERIFY_TOKEN_TIMEOUT_MS;

} // namespace

SessionServer* SessionServer::s_active = nullptr;

SessionServer::SessionServer()
    : m_server(this)
    , m_superClient(this)
    , m_recordClient(this)
    , m_externSender(m_superClient, SubServerType::SESSION, 0)
{
}

SessionServer::~SessionServer()
{
    if (m_db)
    {
        mysql_close(m_db);
        m_db = nullptr;
    }
}

bool SessionServer::initDatabase(const ServerConfig& cfg)
{
    m_db = mysql_init(nullptr);
    if (!m_db)
    {
        LOG_FATAL("会话服初始化 MySQL 句柄失败");
        return false;
    }
    if (!mysql_real_connect(m_db, cfg.dbHost.c_str(), cfg.dbUser.c_str(), cfg.dbPass.c_str(),
                            cfg.dbName.c_str(), static_cast<unsigned int>(cfg.dbPort),
                            nullptr, 0))
    {
        LOG_FATAL("会话服连接数据库失败: %s", mysql_error(m_db));
        mysql_close(m_db);
        m_db = nullptr;
        return false;
    }
    mysql_set_character_set(m_db, "utf8mb4");
    LOG_INFO("会话服数据库连接成功: %s:%d/%s",
             cfg.dbHost.c_str(), cfg.dbPort, cfg.dbName.c_str());
    return true;
}

bool SessionServer::Init(const std::string& ip, uint16_t port,
                         const ServerConfig& cfg, const ServerList& list, uint32_t selfId)
{
    Logger::Instance().SetServerName("SessionServer");
    LOG_INFO("会话服启动中: %s:%d", ip.c_str(), port);
    if (!initDatabase(cfg))
        return false;
    if (const ServerEntry* self = list.find(SubServerType::SESSION, selfId))
        m_self = *self;
    m_externSender.setSelfId(m_self.id ? m_self.id : selfId);
    ServerBootstrap::bindRemoteLog(m_externSender, SubServerType::SESSION);
    wireTlsServer(m_server);
    if (!m_server.Start(ip, port))
    {
        LOG_FATAL("会话服监听启动失败");
        return false;
    }
    wireTlsClient(m_superClient);
    if (!m_superClient.Connect(cfg.superIP, (uint16_t)cfg.superPort))
        LOG_WARN("无法连接超级服");

    const ServerEntry* rec = list.findFirst(SubServerType::RECORD);
    if (!rec)
    {
        LOG_FATAL("服务器列表缺少 RECORD 条目");
        return false;
    }
    wireTlsClient(m_recordClient);
    if (!m_recordClient.Connect(rec->ip, rec->port))
    {
        LOG_FATAL("无法连接存档服: %s:%u", rec->ip.c_str(), rec->port);
        return false;
    }

    registerHandlers();

    TimerMgr::Instance().Register(500, 0, [this] { registerToSuper(); });
    TimerMgr::Instance().Register(10000, 10000, [this] { sendHeartbeat(); });
    TimerMgr::Instance().Register(60000, 60000, [this] { autoSaveAll(); });
    TimerMgr::Instance().Register(1000, 1000, [this] { tickPendingUserLoads(); });
    TimerMgr::Instance().Register(60000, 60000, [this] {
        auto& m = ServiceHealthMetrics::instance();
        LOG_INFO("会话服健康指标: 发送失败=%llu 重连=%llu 待加载用户=%zu",
                 static_cast<unsigned long long>(m.getSendMsgFail()),
                 static_cast<unsigned long long>(m.getReconnectAttempt()),
                 m_pendingUserLoads.size());
    });
    s_active = this;
    LOG_INFO("会话服启动完成（Record 关系预载将在主循环异步进行）");
    return true;
}

void SessionServer::Run()
{
    while (!m_startupFailed)
    {
        tickStartup();
        m_server.Poll(10);
        TimerMgr::Instance().Update();
        m_superClient.Poll(0);
        m_recordClient.Poll(0);
    }
}

bool SessionServer::beginRelationPreload()
{
    if (m_relationPreloadSent)
        return true;

    if (!m_recordClient.canSend())
        return false;

    m_relationPreloadDone = false;
    m_relationPreloadOk   = false;

    Msg_REC_RelationPreloadReq req{};
    if (!m_recordClient.SendMsg(
            (uint16_t)InternalMsgID::REC_RELATION_PRELOAD_REQ,
            reinterpret_cast<char*>(&req), sizeof(req)))
    {
        LOG_ERR("REC_RELATION_PRELOAD_REQ 未发送：SendMsg 失败");
        return false;
    }

    m_relationPreloadSent = true;
    m_relationPreloadDeadlineMs = TimerMgr::NowMs() + RELATION_PRELOAD_TIMEOUT_MS;
    return true;
}

void SessionServer::tickStartup()
{
    if (m_startupComplete || m_startupFailed)
        return;

    if (!m_recordClient.canSend())
        return;

    if (!m_relationPreloadSent)
    {
        if (!beginRelationPreload())
            return;
    }

    if (!m_relationPreloadDone)
    {
        if (TimerMgr::NowMs() > m_relationPreloadDeadlineMs)
        {
            LOG_FATAL("REC_RELATION_PRELOAD_REQ 响应超时（%ums）", RELATION_PRELOAD_TIMEOUT_MS);
            m_startupFailed = true;
        }
        return;
    }

    if (!m_relationPreloadOk)
    {
        LOG_FATAL("从存档服预加载关系数据失败");
        m_startupFailed = true;
        return;
    }

    m_startupComplete = true;
    LOG_INFO("会话服关系数据预载完成");
}

void SessionServer::OnConnect(ConnID id)
{
    if (m_gatewayInboundConn == INVALID_CONN_ID)
        m_gatewayInboundConn = id;
    LOG_INFO("内部连接建立: conn=%u (gateway=%u)", id, m_gatewayInboundConn);
}

void SessionServer::OnDisconnect(ConnID id)
{
    SessionSceneManager::Instance().unbindConn(id);
    LOG_INFO("内部连接断开: conn=%u", id);
}

void SessionServer::OnMessage(ConnID id, uint8_t module, uint8_t sub, const char* data, uint16_t len)
{
    MsgIngress::dispatchInternal(id, module, sub, data, len);
}

void SessionServer::setGatewayInboundConn(ConnID conn)
{
    m_gatewayInboundConn = conn;
}

bool SessionServer::saveRelation(const RelationRowData& row)
{
    std::vector<char> buf;
    RelationWireUtil::appendRow(row, buf);
    return m_recordClient.SendMsg((uint16_t)InternalMsgID::REC_RELATION_SAVE_REQ,
                                  buf.data(), static_cast<uint16_t>(buf.size()));
}

void SessionServer::registerHandlers()
{
    SessionInternMsgRegister(*this);
    SessionClientMsgRegister(*this);
}

void SessionServer::onRelationPreloadRsp(ConnID /*fromConn*/, const char* data, uint16_t len)
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
        LOG_ERR("关系预加载响应失败: code=%d", hdr->code);
        m_relationPreloadOk   = false;
        m_relationPreloadDone = true;
        return;
    }

    std::vector<RelationRowData> rows;
    if (!RelationWireUtil::parseAllRows(data, len, sizeof(Msg_REC_RelationPreloadRsp), rows))
    {
        LOG_ERR("关系预加载响应解析失败");
        m_relationPreloadOk   = false;
        m_relationPreloadDone = true;
        return;
    }

    SessionUserManager::Instance().applyPreloadRows(rows);
    m_relationPreloadOk   = true;
    m_relationPreloadDone = true;
}

void SessionServer::onRelationLoadRsp(ConnID /*fromConn*/, const char* data, uint16_t len)
{
    if (len < sizeof(Msg_REC_RelationLoadRsp))
    {
        return;
    }
    const auto* hdr = reinterpret_cast<const Msg_REC_RelationLoadRsp*>(data);
    std::vector<RelationRowData> rows;
    if (hdr->code != 0
        || !RelationWireUtil::parseAllRows(data, len, sizeof(Msg_REC_RelationLoadRsp), rows)
        || rows.empty())
    {
        finishPendingUserLoad(hdr->userID, false, nullptr);
        return;
    }

    finishPendingUserLoad(hdr->userID, true, &rows[0]);
}

void SessionServer::finishPendingUserLoad(UserID userId, bool ok, const RelationRowData* row)
{
    auto pit = m_pendingUserLoads.find(userId);
    if (pit == m_pendingUserLoads.end())
        return;

    const ConnID replyConn = pit->second.replyConn;
    m_pendingUserLoads.erase(pit);

    if (!ok)
    {
        LOG_WARN("会话用户关系异步加载失败: userID=%llu", static_cast<unsigned long long>(userId));
        return;
    }

    auto user = SessionUserManager::Instance().findUser(userId);
    if (!user)
        return;

    user->applyRelationRow(*row);
    user->onOnline();

    if (!m_server.SendMsg(replyConn, (uint16_t)InternalMsgID::SES_LOAD_USER_RSP,
                          reinterpret_cast<const char*>(&userId), sizeof(userId)))
    {
        ServiceHealthMetrics::instance().incSendMsgFail();
        LOG_WARN("会话用户加载响应发送失败: userID=%llu conn=%u",
                 static_cast<unsigned long long>(userId), replyConn);
    }
}

void SessionServer::tickPendingUserLoads()
{
    const uint64_t nowMs = TimerMgr::NowMs();
    std::vector<UserID> expired;
    expired.reserve(m_pendingUserLoads.size());
    for (const auto& [userId, pending] : m_pendingUserLoads)
    {
        if (nowMs >= pending.deadlineMs)
            expired.push_back(userId);
    }
    for (UserID userId : expired)
    {
        LOG_WARN("会话用户关系加载超时: userID=%llu", static_cast<unsigned long long>(userId));
        finishPendingUserLoad(userId, false, nullptr);
    }
}

void SessionServer::registerToSuper()
{
    if (!m_superClient.canSend())
        return;
    Msg_S2S_Register reg{};
    reg.serverType = (uint8_t)SubServerType::SESSION;
    reg.serverID = m_self.id;
    copyToWire(reg.ip, sizeof(reg.ip), m_self.ip.c_str());
    reg.port = m_self.port;
    copyToWire(reg.name, sizeof(reg.name), m_self.name.c_str());
    m_superClient.SendMsg((uint16_t)InternalMsgID::S2S_REGISTER_REQ,
                          reinterpret_cast<char*>(&reg), sizeof(reg));
}

void SessionServer::sendHeartbeat()
{
    if (!m_superClient.canSend())
        return;
    Msg_S2S_Heartbeat hb{};
    hb.seq = ++m_hbSeq;
    hb.timestamp = TimerMgr::NowMs();
    m_superClient.SendMsg((uint16_t)InternalMsgID::S2S_HEARTBEAT,
                          reinterpret_cast<char*>(&hb), sizeof(hb));
}

void SessionServer::onLoadUserReq(ConnID fromConn, const char* data, uint16_t len)
{
    if (len < sizeof(UserID))
        return;
    UserID uid = *reinterpret_cast<const UserID*>(data);

    auto user = SessionUserManager::Instance().getOrCreateUser(uid);
    user->init();

    if (user->hasRelationLoaded())
    {
        user->onOnline();
        if (!m_server.SendMsg(fromConn, (uint16_t)InternalMsgID::SES_LOAD_USER_RSP,
                              data, len))
        {
            ServiceHealthMetrics::instance().incSendMsgFail();
            LOG_WARN("会话用户加载响应发送失败(缓存命中): userID=%llu conn=%u",
                     static_cast<unsigned long long>(uid), fromConn);
        }
        return;
    }

    if (m_pendingUserLoads.count(uid) > 0)
        return;

    PendingUserLoad pending{};
    pending.replyConn = fromConn;
    pending.deadlineMs = TimerMgr::NowMs() + RELATION_ASYNC_LOAD_TIMEOUT_MS;
    m_pendingUserLoads[uid] = pending;

    if (!m_recordClient.SendMsg((uint16_t)InternalMsgID::REC_RELATION_LOAD_REQ,
                                reinterpret_cast<const char*>(&uid), sizeof(uid)))
    {
        m_pendingUserLoads.erase(uid);
        ServiceHealthMetrics::instance().incSendMsgFail();
        LOG_WARN("会话用户关系加载请求发送失败: userID=%llu", static_cast<unsigned long long>(uid));
    }
}

void SessionServer::onSaveUserReq(ConnID /*fromConn*/, const char* data, uint16_t len)
{
    if (len < sizeof(UserID))
        return;
    UserID uid = *reinterpret_cast<const UserID*>(data);
    auto user = SessionUserManager::Instance().findUser(uid);
    if (!user)
        user = SessionUserManager::Instance().getOrCreateUser(uid);
    user->save(*this);
}

void SessionServer::onFriendUpdate(ConnID /*fromConn*/, const char* /*data*/, uint16_t len)
{
    LOG_DEBUG("好友更新消息长度: len=%d", len);
}

void SessionServer::onSceneRegisterReq(ConnID fromConn, const Msg_SES_SceneRegisterReq& req)
{
    SessionSceneManager::Instance().registerScene(fromConn, req);

    Msg_SES_SceneRegisterRsp rsp{};
    rsp.code = 0;
    rsp.sceneInstanceId = req.sceneInstanceId;
    m_server.SendMsg(fromConn, (uint16_t)InternalMsgID::SES_SCENE_REGISTER_RSP,
                     reinterpret_cast<char*>(&rsp), sizeof(rsp));
}

void SessionServer::onSceneUnregister(ConnID /*fromConn*/, const Msg_SES_SceneUnregister& req)
{
    SessionSceneManager::Instance().unregisterScene(req.sceneInstanceId, req.sceneServerId);
}

void SessionServer::onResolveMapReq(ConnID /*fromConn*/, const Msg_SES_ResolveMapReq& req)
{
    Msg_SES_ResolveMapRsp rsp{};
    rsp.userID = req.userID;
    rsp.mapId = req.mapId;
    rsp.sceneServerId = SessionSceneManager::Instance().resolveSceneServerByMapId(req.mapId);
    rsp.code = rsp.sceneServerId != 0 ? 0 : -1;

    // Super 经 Session 的 superClient 入站下发 SES_RESOLVE_MAP_REQ，须同 superClient 回包
    // （勿用 m_server.SendMsg(fromConn)：fromConn 为 listen 口 conn，与 Super 链路无关）
    m_superClient.SendMsg(static_cast<uint16_t>(InternalMsgID::SES_RESOLVE_MAP_RSP),
                          reinterpret_cast<char*>(&rsp), sizeof(rsp));
    LOG_INFO("地图解析结果: user=%llu map=%u -> sceneServerId=%u code=%d",
             req.userID, req.mapId, rsp.sceneServerId, rsp.code);
    char detail[64];
    snprintf(detail, sizeof(detail), "地图解析 map=%u scene=%u", req.mapId, rsp.sceneServerId);
    logLoginFlow(LoginFlowPhase::SUPER_ENTER, 0, req.userID, 0, rsp.code, detail);
}

void SessionServer::onSceneMapLoadReport(ConnID /*fromConn*/, const Msg_SES_SceneMapLoadReport& req)
{
    if (req.mapId == 0)
        SessionSceneManager::Instance().reportServerPlayerCount(req.sceneServerId, req.playerCount);
    else
        SessionSceneManager::Instance().reportMapPlayerCount(req.sceneServerId, req.mapId,
                                                               req.playerCount);
}

void SessionServer::onCopyCreateReq(ConnID fromConn, const Msg_SES_CopyCreateReq& req)
{

    Msg_SES_CopyCreateRsp rsp{};
    rsp.code = 0;

    const CopyType copyType = static_cast<CopyType>(req.copyType);
    SessionCopyScene* existing =
        SessionSceneManager::Instance().findReusableCopy(copyType, req.mapId, req.ownerId);

    if (existing)
    {
        rsp.targetSceneServerId = existing->getSceneServerId();
        rsp.copyInstanceId = existing->getCopyInstanceId();
        rsp.copyType = req.copyType;
        rsp.mapId = req.mapId;
        rsp.ownerId = req.ownerId;
        rsp.maxPlayer = existing->getMaxPlayer();
        rsp.reused = 1;
        copyWireField(rsp.mapName, req.mapName);
    }
    else
    {
        uint32_t targetId = SessionSceneManager::Instance().pickSceneServerId();
        if (targetId == 0)
            targetId = req.reqSceneServerId;

        const uint64_t copyId = SessionSceneManager::Instance().generateCopyInstanceId();
        SessionSceneManager::Instance().createCopyRecord(targetId, copyId, req);

        rsp.targetSceneServerId = targetId;
        rsp.copyInstanceId = copyId;
        rsp.copyType = req.copyType;
        rsp.mapId = req.mapId;
        rsp.ownerId = req.ownerId;
        rsp.maxPlayer = req.maxPlayer;
        rsp.reused = 0;
        copyWireField(rsp.mapName, req.mapName);

        Msg_SES_CopyCreateCmd cmd{};
        cmd.copyInstanceId = copyId;
        cmd.copyType = req.copyType;
        cmd.mapId = req.mapId;
        cmd.ownerId = req.ownerId;
        cmd.maxPlayer = req.maxPlayer;
        copyWireField(cmd.mapName, req.mapName);

        ConnID targetConn = SessionSceneManager::Instance().findConnBySceneServerId(targetId);
        if (targetConn != INVALID_CONN_ID)
        {
            m_server.SendMsg(targetConn, (uint16_t)InternalMsgID::SES_COPY_CREATE_CMD,
                             reinterpret_cast<char*>(&cmd), sizeof(cmd));
        }
        else
        {
            LOG_WARN("副本创建命令下发失败: 目标场景服 %u 未连接", targetId);
            rsp.code = -1;
        }
    }

    m_server.SendMsg(fromConn, (uint16_t)InternalMsgID::SES_COPY_CREATE_RSP,
                     reinterpret_cast<char*>(&rsp), sizeof(rsp));
}

void SessionServer::autoSaveAll()
{
    SessionUserManager::Instance().forEach([this](UserID uid, const std::shared_ptr<SessionUser>& user) {
        (void)uid;
        if (user->needSave())
            user->save(*this);
    });
}

bool SessionServer::SendToClient(uint32_t clientConnID, uint8_t module, uint8_t sub,
                                 const char* data, uint16_t len)
{
    if (relaySendToClientViaGateway(m_server, m_gatewayInboundConn,
                                    clientConnID, module, sub, data, len))
        return true;
    LOG_WARN("下发客户端失败: 无网关入站连接 clientConn=%u", clientConnID);
    return false;
}

bool SessionServer::SendToClient(uint32_t clientConnID, uint16_t flatMsgId,
                                 const char* data, uint16_t len)
{
    return SendToClient(clientConnID,
                        static_cast<uint8_t>(flatMsgId >> 8),
                        static_cast<uint8_t>(flatMsgId & 0xFF),
                        data, len);
}
