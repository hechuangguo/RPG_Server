/**
 * @file GatewayServer.cpp
 * @brief GatewayServer 非内联方法实现。
 */

#include "GatewayServer.h"
#include "GatewayInternMsgRegister.h"
#include "GatewayClientMsgRegister.h"
#include "../sdk/net/MsgIngress.h"
#include "../sdk/util/LoginEnterErrorCode.h"
#include "../sdk/util/LoginFlowLog.h"
#include "../sdk/net/ClientWireSend.h"
#include "../sdk/net/NetTls.h"
#include "../sdk/net/GwClientRelay.h"

#include <vector>
#include <cstdio>
#include <cstring>
#include <memory>
#include <unordered_set>

namespace {

constexpr uint64_t UPSTREAM_CONNECT_TIMEOUT_MS = 5000;
constexpr uint64_t GATEWAY_AUTH_TIMEOUT_MS = 10000;

/** @brief 区内服出站 TcpClient 回调：仅派发消息，不创建客户端会话 */
class GatewayUpstreamCallback : public INetCallback
{
public:
    void OnConnect(ConnID) override {}

    void OnDisconnect(ConnID) override {}

    void OnMessage(ConnID id, uint8_t module, uint8_t sub,
                   const char* data, uint16_t len) override
    {
        MsgIngress::dispatchInternal(id, module, sub, data, len);
    }
};

GatewayUpstreamCallback g_upstreamCb;

} // namespace

GatewayServer::GatewayServer()
    : m_clientServer(this)
    , m_superClient(&g_upstreamCb)
    , m_recordClient(&g_upstreamCb)
    , m_sessionClient(&g_upstreamCb)
    , m_scenePool(&g_upstreamCb)
{
}

bool GatewayServer::Init(uint16_t clientPort,
                         const ServerConfig& cfg, const ServerList& list, uint32_t selfId)
{
    Logger::Instance().SetServerName("GatewayServer");
    LOG_INFO("网关服启动中: clientPort=%d", clientPort);

    m_serverList = list;
    if (const ServerEntry* self = list.find(SubServerType::GATEWAY, selfId))
        m_self = *self;

    wireTlsServer(m_clientServer, false);
    if (!m_clientServer.Start("0.0.0.0", clientPort))
    { LOG_FATAL("客户端监听失败"); return false; }

    wireTlsClient(m_superClient);
    m_superIP = cfg.superIP;
    m_superPort = (uint16_t)cfg.superPort;
    m_superClient.Connect(m_superIP, m_superPort);
    m_clientPort = clientPort;
    m_zoneId = cfg.zoneId;
    m_gameType = cfg.gameType;

    registerHandlers();
    scheduleSuperRegister();
    TimerMgr::Instance().Register(5000, 5000, [this] {
        if (m_superClient.IsConnected())
            return;
        LOG_WARN("超级服出站已断开，尝试重连");
        tryReconnectSuper();
    });
    TimerMgr::Instance().Register(10000, 10000, [this]{ sendHeartbeat(); });
    TimerMgr::Instance().Register(30000, 30000, [this]{ checkTimeout(); });
    TimerMgr::Instance().Register(10000, 10000, [this]{ sendLoginGatewayHeartbeat(); });

    LOG_INFO("网关服启动完成（等待 S2S_REGISTER_RSP 后连接上游）");
    return true;
}

void GatewayServer::Run()
{
    while (true)
    {
        m_clientServer.Poll(5);
        TimerMgr::Instance().Update();
        /** 定时器回调 SendMsg 后统一 Poll 出站，避免同迭代内重复 Poll */
        m_superClient.Poll(0);
        if (m_upstreamReady)
        {
            m_recordClient.Poll(0);
            m_sessionClient.Poll(0);
            m_scenePool.pollAll();
        }
    }
}

void GatewayServer::OnConnect(ConnID id)
{
    m_userManager.addUser(id);
    LOG_INFO("客户端连接建立: connID=%u", id);
}

void GatewayServer::OnDisconnect(ConnID id)
{
    auto user = m_userManager.findUser(id);
    if (user)
    {
        LOG_INFO("客户端连接断开: connID=%u userID=%llu state=%d",
                 id, user->GetID(),
                 static_cast<int>(user->getClientState()));
        if (user->GetID() != INVALID_USER_ID)
            leaveWorldSession(user, true, "客户端TCP断开");
        m_userManager.removeUser(id);
    }
}

void GatewayServer::OnMessage(ConnID id, uint8_t module, uint8_t sub,
                              const char* data, uint16_t len)
{
    if (m_userManager.findUser(id))
        handleClientMsg(id, module, sub, data, len);
    else
        MsgIngress::dispatchInternal(id, module, sub, data, len);
}

void GatewayServer::registerHandlers()
{
    GatewayInternMsgRegister(*this);
    GatewayClientMsgRegister(*this);
}

void GatewayServer::onSuperRegisterRsp(ConnID /*fromConn*/, const char* /*data*/, uint16_t /*len*/)
{
    if (!m_upstreamReady)
    {
        LOG_INFO("收到 S2S_REGISTER_RSP，开始建立上游连接");
        setupUpstreamClients();
        return;
    }
    /** Super 重连后需重新上报；已成功上报则忽略重复 REGISTER_RSP */
    if (!m_reportedToLogin)
        reportGatewayToSuper();
}

void GatewayServer::setupUpstreamClients()
{
    if (m_upstreamReady)
        return;

    if (const ServerEntry* rec = m_serverList.findFirst(SubServerType::RECORD))
    {
        wireTlsClient(m_recordClient);
        m_recordClient.Connect(rec->ip, rec->port);
    }
    else
        LOG_WARN("服务器列表缺少 RECORD 条目");

    if (const ServerEntry* ses = m_serverList.findFirst(SubServerType::SESSION))
    {
        wireTlsClient(m_sessionClient);
        m_sessionClient.Connect(ses->ip, ses->port);
    }
    else
        LOG_WARN("服务器列表缺少 SESSION 条目");

    if (!m_scenePool.connectAll(m_serverList))
        LOG_WARN("未建立任何 SCENE 连接");

    pollUpstreamUntilReady();

    m_upstreamReady = true;
    LOG_INFO("网关上游就绪: record=%d session=%d scene=%d",
             m_recordClient.IsConnected() ? 1 : 0,
             m_sessionClient.IsConnected() ? 1 : 0,
             m_scenePool.hasAnyConnected() ? 1 : 0);

    reportGatewayToSuper();
}

void GatewayServer::pollUpstreamUntilReady()
{
    const uint64_t deadline = TimerMgr::NowMs() + UPSTREAM_CONNECT_TIMEOUT_MS;
    while (TimerMgr::NowMs() < deadline)
    {
        m_recordClient.Poll(10);
        m_sessionClient.Poll(10);
        m_scenePool.pollAll();
        m_superClient.Poll(0);
        m_clientServer.Poll(0);
        if (m_recordClient.canSend() && m_sessionClient.canSend() &&
            m_scenePool.hasAnyCanSend())
        {
            return;
        }
    }
    LOG_WARN("网关上游连接超时（可能仅部分连通）");
}

void GatewayServer::reportGatewayToSuper()
{
    if (!m_superClient.canSend())
    {
        LOG_WARN("超级服未连接，跳过网关注册");
        return;
    }

    Msg_SS_LoginGatewayWrap wrap{};
    wrap.gatewayConnID = 0;
    wrap.body.gatewayServerId = m_self.id;
    wrap.body.zoneId = m_zoneId;
    wrap.body.gameType = m_gameType;
    wrap.body.port = m_clientPort;
    wrap.body.onlineCount = static_cast<uint32_t>(m_userManager.getUserCount());
    copyToWire(wrap.body.ip, sizeof(wrap.body.ip),
               m_self.ip.empty() ? "127.0.0.1" : m_self.ip.c_str());
    copyToWire(wrap.body.name, sizeof(wrap.body.name), m_self.name.c_str());
    copyToWire(wrap.body.zoneName, sizeof(wrap.body.zoneName), "game-zone");

    m_superClient.SendMsg(static_cast<uint16_t>(InternalMsgID::SS_LOGIN_GATEWAY_WRAP_REQ),
                          reinterpret_cast<char*>(&wrap), sizeof(wrap));
    LOG_INFO("已发送 SS_LOGIN_GATEWAY_WRAP: id=%u %s:%u",
             wrap.body.gatewayServerId, wrap.body.ip, wrap.body.port);
}

void GatewayServer::sendLoginGatewayHeartbeat()
{
    if (!m_upstreamReady || !m_superClient.canSend())
        return;
    if (!m_reportedToLogin)
    {
        reportGatewayToSuper();
        if (!m_reportedToLogin)
            return;
    }

    Msg_Login_GatewayRegister hb{};
    hb.gatewayServerId = m_self.id;
    hb.zoneId = m_zoneId;
    hb.gameType = m_gameType;
    hb.port = m_clientPort;
    hb.onlineCount = static_cast<uint32_t>(m_userManager.getUserCount());
    copyToWire(hb.ip, sizeof(hb.ip),
               m_self.ip.empty() ? "127.0.0.1" : m_self.ip.c_str());
    copyToWire(hb.name, sizeof(hb.name), m_self.name.c_str());
    m_superClient.SendMsg(static_cast<uint16_t>(InternalMsgID::LOGIN_GATEWAY_HEARTBEAT),
                          reinterpret_cast<char*>(&hb), sizeof(hb));
}

void GatewayServer::onLoginGatewayWrapRsp(ConnID /*fromConn*/, const Msg_SS_LoginGatewayWrapRsp& rsp)
{
    if (rsp.body.code == 0)
    {
        m_reportedToLogin = true;
        LOG_INFO("登录网关注册回包成功: gatewayId=%u", rsp.body.gatewayServerId);
    }
    else
    {
        m_reportedToLogin = false;
        LOG_WARN("登录网关注册回包失败: code=%d gatewayId=%u（下次心跳将重试注册）",
                 rsp.body.code, rsp.body.gatewayServerId);
    }
}

void GatewayServer::handleClientMsg(ConnID connID, uint8_t module, uint8_t sub,
                                    const char* data, uint16_t len)
{
    auto user = m_userManager.findUser(connID);
    if (!user) return;
    user->touchHeartbeat();

    if (user->getClientState() == ClientState::CONNECTED && !user->isFirstUplinkLogged())
    {
        user->setFirstUplinkLogged(true);
        LOG_INFO("客户端首条上行: conn=%u mod=0x%02X sub=0x%02X len=%u",
                 connID, module, sub, len);
    }

    const ValidateResult vr =
        ClientMsgValidator::check(user.get(), module, sub, data, len);
    if (vr != ValidateResult::OK)
    {
        sendClientError(connID, vr);
        LOG_WARN("客户端消息被拒绝: conn=%u mod=0x%02X sub=0x%02X vr=%u",
                 connID, module, sub, static_cast<unsigned>(vr));
        return;
    }

    const ClientForwardTarget target = ClientMsgRouter::resolve(module, sub);
    switch (target)
    {
    case ClientForwardTarget::LOCAL:
        if (!MsgIngress::dispatchClient(connID, module, sub, data, len))
            sendClientError(connID, ValidateResult::UNKNOWN_MSG);
        break;
    case ClientForwardTarget::SCENE:
        if (!m_upstreamReady)
        {
            sendClientError(connID, ValidateResult::BAD_STATE);
            break;
        }
        {
            SceneClient* scene = m_scenePool.clientFor(user->getSceneServerId());
            if (!scene || !scene->forwardClientMsg(connID, module, sub, data, len))
                sendClientError(connID, ValidateResult::BAD_STATE);
        }
        break;
    case ClientForwardTarget::SESSION:
        if (!m_upstreamReady)
            sendClientError(connID, ValidateResult::BAD_STATE);
        else
            forwardClientMsg(m_sessionClient, connID, module, sub, data, len);
        break;
    case ClientForwardTarget::DROP:
    default:
        sendClientError(connID, ValidateResult::UNKNOWN_MSG);
        break;
    }
}

void GatewayServer::sendClientError(ConnID connID, ValidateResult vr)
{
    Msg_S2C_Error err{};
    err.code = ClientMsgValidator::toErrorCode(vr);
    const char* text = "请求被拒绝";
    switch (vr)
    {
    case ValidateResult::UNKNOWN_MSG:  text = "未知消息"; break;
    case ValidateResult::BAD_LENGTH:   text = "包长非法"; break;
    case ValidateResult::BAD_STATE:    text = "客户端状态不允许"; break;
    case ValidateResult::BAD_PAYLOAD:  text = "包体非法"; break;
    case ValidateResult::RATE_LIMITED: text = "请求过于频繁"; break;
    default: break;
    }
    copyToWire(err.msg, sizeof(err.msg), text);
    sendClientWire(m_clientServer, connID, err);
}

void GatewayServer::onGatewayAuth(ConnID connID, const char* data, uint16_t len)
{
    if (!m_upstreamReady || !m_recordClient.IsConnected())
    {
        sendClientError(connID, ValidateResult::BAD_STATE);
        logLoginFlow(LoginFlowPhase::GATEWAY_AUTH, 0, 0, connID,
                     static_cast<int32_t>(ValidateResult::BAD_STATE), "上游未就绪");
        return;
    }
    if (len < sizeof(Msg_C2S_GatewayAuthReq))
        return;
    const auto* req = reinterpret_cast<const Msg_C2S_GatewayAuthReq*>(data);
    auto user = m_userManager.findUser(connID);
    if (!user)
    {
        sendClientError(connID, ValidateResult::BAD_STATE);
        return;
    }
    user->setZoneId(req->zoneId);
    user->setGameType(req->gameType);
    user->setClientState(ClientState::AUTHING);

    Msg_REC_ValidateTokenReq verifyReq{};
    copyToWire(verifyReq.loginToken, sizeof(verifyReq.loginToken), req->loginToken);
    verifyReq.zoneId = req->zoneId;
    verifyReq.gameType = req->gameType;
    verifyReq.gatewayConnID = connID;
    m_recordClient.SendMsg(static_cast<uint16_t>(InternalMsgID::REC_VALIDATE_TOKEN_REQ),
                           reinterpret_cast<char*>(&verifyReq), sizeof(verifyReq));
    char account[sizeof(req->account)];
    copyToWire(account, sizeof(account), req->account);
    LOG_INFO("Gateway 票据鉴权: account=%s conn=%u zone=%u", account, connID, req->zoneId);
    logLoginFlow(LoginFlowPhase::GATEWAY_AUTH, 0, 0, connID, 0, "发起 token 校验");
}

void GatewayServer::onSelectUser(ConnID connID, const char* data, uint16_t len)
{
    if (len < sizeof(Msg_C2S_SelectUserReq))
        return;
    const auto* req = reinterpret_cast<const Msg_C2S_SelectUserReq*>(data);
    auto user = m_userManager.findUser(connID);
    if (!user)
        return;

    if (user->getAccid() == 0)
    {
        sendClientError(connID, ValidateResult::BAD_STATE);
        logLoginFlow(LoginFlowPhase::CHAR_SELECT, 0, req->userID, connID,
                     static_cast<int32_t>(ValidateResult::BAD_STATE), "账号未鉴权");
        return;
    }

    if (!user->isRoleListReady())
    {
        sendClientError(connID, ValidateResult::BAD_STATE);
        logLoginFlow(LoginFlowPhase::CHAR_SELECT, user->getAccid(), req->userID, connID,
                     static_cast<int32_t>(ValidateResult::BAD_STATE), "角色列表未就绪");
        return;
    }

    if (!user->ownsRole(req->userID))
    {
        sendClientError(connID, ValidateResult::BAD_PAYLOAD);
        logLoginFlow(LoginFlowPhase::CHAR_SELECT, user->getAccid(), req->userID, connID,
                     static_cast<int32_t>(ValidateResult::BAD_PAYLOAD), "选角归属校验失败");
        return;
    }

    uint64_t txnId = req->loginTxnId;
    if (txnId == 0)
    {
        if (user->GetID() == req->userID && user->getLoginTxnId() != 0)
            txnId = user->getLoginTxnId();
        else
            txnId = (TimerMgr::NowMs() << 16) ^ static_cast<uint64_t>(connID) ^ req->userID;
    }

    user->setUserId(req->userID);
    user->setLoginTxnId(txnId);
    user->setClientState(ClientState::ENTERING);

    Msg_GW_UserEnterReq enterReq{};
    enterReq.userID = req->userID;
    enterReq.gatewayClientConnID = connID;
    enterReq.loginTxnId = txnId;
    m_superClient.SendMsg(static_cast<uint16_t>(InternalMsgID::GW_USER_LOGIN_REQ),
                          reinterpret_cast<char*>(&enterReq), sizeof(enterReq));
    LOG_INFO("选角进世界: conn=%u userID=%llu txn=%llu", connID,
             static_cast<unsigned long long>(req->userID),
             static_cast<unsigned long long>(txnId));
    logLoginFlow(LoginFlowPhase::CHAR_SELECT, user->getAccid(), req->userID, connID, 0,
                 nullptr, txnId);
}

void GatewayServer::onCreateUser(ConnID connID, const char* data, uint16_t len)
{
    if (len < sizeof(Msg_C2S_CreateUserReq))
        return;
    if (!m_recordClient.IsConnected())
    {
        Msg_S2C_CreateUserRsp createRsp{};
        initClientMsg(createRsp);
        createRsp.code = static_cast<int32_t>(CreateCharacterError::SYSTEM_ERROR);
        copyToWire(createRsp.msg, sizeof(createRsp.msg), "创角服务不可用");
        m_clientServer.SendMsg(connID, Msg_S2C_CreateUserRsp::kModule, Msg_S2C_CreateUserRsp::kSub,
                               reinterpret_cast<char*>(&createRsp), sizeof(createRsp));
        return;
    }
    const auto* req = reinterpret_cast<const Msg_C2S_CreateUserReq*>(data);
    auto user = m_userManager.findUser(connID);
    if (!user || user->getAccid() == 0)
        return;
    if (user->getClientState() != ClientState::ACCOUNT_OK)
    {
        sendClientError(connID, ValidateResult::BAD_STATE);
        return;
    }

    Msg_REC_CreateCharacterReq createReq{};
    createReq.accid = user->getAccid();
    createReq.zoneId = user->getZoneId();
    copyToWire(createReq.name, sizeof(createReq.name), req->name);
    createReq.vocation = req->vocation;
    createReq.sex = req->sex;
    createReq.gatewayConnID = connID;
    m_recordClient.SendMsg(static_cast<uint16_t>(InternalMsgID::REC_CREATE_CHARACTER_REQ),
                           reinterpret_cast<char*>(&createReq), sizeof(createReq));
    logLoginFlow(LoginFlowPhase::CHAR_CREATE, user->getAccid(), 0, connID, 0, req->name);
}

void GatewayServer::leaveWorldSession(const std::shared_ptr<GatewayUser>& user, bool notifySuper,
                                      const char* reason)
{
    if (!user || user->GetID() == INVALID_USER_ID)
        return;

    const UserID uid = user->GetID();
    const ConnID connId = user->getConnId();

    if (m_upstreamReady && user->getSceneServerId() != 0)
    {
        SceneClient* scene = m_scenePool.clientFor(user->getSceneServerId());
        if (scene)
        {
            scene->sendMsg(static_cast<uint16_t>(InternalMsgID::SCE_USER_LEAVE),
                           reinterpret_cast<const char*>(&uid), sizeof(UserID));
        }
    }

    if (notifySuper && m_superClient.IsConnected())
    {
        Msg_GW_UserLeaveReq leaveReq{};
        leaveReq.userID = uid;
        leaveReq.gatewayClientConnID = connId;
        m_superClient.SendMsg(static_cast<uint16_t>(InternalMsgID::GW_USER_LEAVE_REQ),
                              reinterpret_cast<char*>(&leaveReq), sizeof(leaveReq));
    }

    logLoginFlow(LoginFlowPhase::CHAR_LEAVE, user->getAccid(), uid, connId, 0, reason,
                 user->getLoginTxnId());
}

void GatewayServer::clearInWorldUserState(const std::shared_ptr<GatewayUser>& user)
{
    if (!user)
        return;

    user->setUserId(INVALID_USER_ID);
    user->setSceneServerId(0);
    user->setLoginTxnId(0);
}

void GatewayServer::resetToAccountSession(const std::shared_ptr<GatewayUser>& user)
{
    if (!user)
        return;

    clearInWorldUserState(user);
    user->setOwnedRoleIds({});
    user->setRoleListReady(false);
    user->setClientState(ClientState::ACCOUNT_OK);
}

void GatewayServer::onLogoutReq(ConnID connID, const char* data, uint16_t len)
{
    if (len < sizeof(Msg_C2S_LogoutReq))
        return;

    const auto* req = reinterpret_cast<const Msg_C2S_LogoutReq*>(data);
    auto user = m_userManager.findUser(connID);
    if (!user)
        return;

    const auto action = static_cast<LogoutAction>(req->action);
    Msg_S2C_LogoutRsp rsp{};
    initClientMsg(rsp);
    rsp.action = req->action;

    if (action != LogoutAction::RETURN_CHAR_SELECT &&
        action != LogoutAction::RETURN_LOGIN)
    {
        rsp.code = -1;
        copyToWire(rsp.msg, sizeof(rsp.msg), "无效的退出选项");
        m_clientServer.SendMsg(connID, Msg_S2C_LogoutRsp::kModule, Msg_S2C_LogoutRsp::kSub,
                               reinterpret_cast<char*>(&rsp), sizeof(rsp));
        logLoginFlow(LoginFlowPhase::LOGOUT, user->getAccid(), user->GetID(), connID, -1,
                     "无效 action");
        return;
    }

    const uint64_t accid = user->getAccid();
    const uint32_t zoneId = user->getZoneId();
    const UserID leavingUserId = user->GetID();

    LOG_INFO("客户端退出请求: conn=%u userID=%llu action=%s",
             connID, leavingUserId,
             action == LogoutAction::RETURN_CHAR_SELECT ? "回选角" : "回登录");

    leaveWorldSession(user, true,
                      action == LogoutAction::RETURN_CHAR_SELECT ? "回选角前离世界"
                                                                 : "回登录前离世界");
    resetToAccountSession(user);

    rsp.code = 0;
    copyToWire(rsp.msg, sizeof(rsp.msg),
               action == LogoutAction::RETURN_CHAR_SELECT ? "已返回选角" : "已退出游戏");
    m_clientServer.SendMsg(connID, Msg_S2C_LogoutRsp::kModule, Msg_S2C_LogoutRsp::kSub,
                           reinterpret_cast<char*>(&rsp), sizeof(rsp));

    logLoginFlow(LoginFlowPhase::LOGOUT, accid, leavingUserId, connID, 0,
                 action == LogoutAction::RETURN_CHAR_SELECT ? "返回选角" : "返回登录");

    if (action == LogoutAction::RETURN_CHAR_SELECT)
        sendUserListToClient(connID, accid, zoneId);
}

bool GatewayServer::sendUserListToClient(ConnID clientConn, uint64_t accid, uint32_t zoneId,
                                         bool notifyClientOnRecordDown)
{
    if (!m_recordClient.IsConnected())
    {
        LOG_WARN("请求角色列表失败: Record 未连接 conn=%u accid=%llu",
                 clientConn, static_cast<unsigned long long>(accid));
        if (notifyClientOnRecordDown)
        {
            Msg_S2C_UserListHeader outHdr{};
            initClientMsg(outHdr);
            outHdr.code = -1;
            outHdr.count = 0;
            m_clientServer.SendMsg(clientConn, Msg_S2C_UserListHeader::kModule,
                                   Msg_S2C_UserListHeader::kSub,
                                   reinterpret_cast<char*>(&outHdr), sizeof(outHdr));
        }
        return false;
    }
    Msg_REC_ListCharactersReq listReq{};
    listReq.accid = accid;
    listReq.zoneId = zoneId;
    listReq.gatewayConnID = clientConn;
    m_recordClient.SendMsg(static_cast<uint16_t>(InternalMsgID::REC_LIST_CHARACTERS_REQ),
                           reinterpret_cast<char*>(&listReq), sizeof(listReq));
    return true;
}

void GatewayServer::onValidateTokenRsp(ConnID /*fromConn*/, const Msg_REC_ValidateTokenRsp& rsp)
{
    ConnID clientConn = rsp.gatewayConnID;
    auto user = m_userManager.findUser(clientConn);
    if (!user)
        return;

    if (rsp.code != 0)
    {
        Msg_S2C_LoginRsp loginRsp{};
        initClientMsg(loginRsp);
        loginRsp.code = 1;
        copyToWire(loginRsp.msg, sizeof(loginRsp.msg), "登录票据无效或已过期");
        user->setClientState(ClientState::CONNECTED);
        user->setAccid(0);
        user->setRoleListReady(false);
        m_clientServer.SendMsg(clientConn, Msg_S2C_LoginRsp::kModule, Msg_S2C_LoginRsp::kSub,
                               reinterpret_cast<char*>(&loginRsp), sizeof(loginRsp));
        LOG_WARN("Gateway 票据鉴权失败: conn=%u", clientConn);
        logLoginFlow(LoginFlowPhase::GATEWAY_AUTH, 0, 0, clientConn, rsp.code, nullptr);
        m_clientServer.Kick(clientConn);
        m_userManager.removeUser(clientConn);
        return;
    }

    user->setRoleListReady(false);

    // Record 不可达：下发 S2C_USER_LIST code=-1（与创角后刷新一致）并 S2C_LOGIN_RSP code=-1
    if (!sendUserListToClient(clientConn, rsp.accid, user->getZoneId(), true))
    {
        user->setClientState(ClientState::CONNECTED);
        Msg_S2C_LoginRsp loginRsp{};
        initClientMsg(loginRsp);
        loginRsp.code = -1;
        copyToWire(loginRsp.msg, sizeof(loginRsp.msg), "存档服务不可用，请稍后重试");
        m_clientServer.SendMsg(clientConn, Msg_S2C_LoginRsp::kModule, Msg_S2C_LoginRsp::kSub,
                               reinterpret_cast<char*>(&loginRsp), sizeof(loginRsp));
        LOG_WARN("Gateway 鉴权后无法拉取角色列表: Record 未连接 conn=%u", clientConn);
        logLoginFlow(LoginFlowPhase::GATEWAY_AUTH, rsp.accid, 0, clientConn, -1,
                     "Record 未连接");
        m_clientServer.Kick(clientConn);
        m_userManager.removeUser(clientConn);
        return;
    }

    user->setAccid(rsp.accid);
    user->setClientState(ClientState::ACCOUNT_OK);

    Msg_S2C_LoginRsp loginRsp{};
    initClientMsg(loginRsp);
    loginRsp.code = 0;
    loginRsp.userID = 0;
    loginRsp.accid = rsp.accid;
    loginRsp.loginToken[0] = '\0';
    loginRsp.tokenExpireMs = 0;
    copyToWire(loginRsp.msg, sizeof(loginRsp.msg), "网关鉴权成功");
    m_clientServer.SendMsg(clientConn, Msg_S2C_LoginRsp::kModule, Msg_S2C_LoginRsp::kSub,
                           reinterpret_cast<char*>(&loginRsp), sizeof(loginRsp));

    logLoginFlow(LoginFlowPhase::GATEWAY_AUTH, rsp.accid, 0, clientConn, 0, "鉴权成功");
}

void GatewayServer::onListCharactersRsp(ConnID /*fromConn*/, const char* data, uint16_t len)
{
    if (len < sizeof(Msg_REC_ListCharactersRspHeader))
        return;
    const auto* hdr = reinterpret_cast<const Msg_REC_ListCharactersRspHeader*>(data);
    ConnID clientConn = hdr->gatewayConnID;
    auto user = m_userManager.findUser(clientConn);
    if (!user)
    {
        LOG_WARN("角色列表回包丢弃: 会话不存在 conn=%u", clientConn);
        return;
    }
    if (hdr->code != 0)
    {
        Msg_S2C_UserListHeader outHdr{};
        initClientMsg(outHdr);
        outHdr.code = hdr->code;
        outHdr.count = 0;
        m_clientServer.SendMsg(clientConn, Msg_S2C_UserListHeader::kModule,
                               Msg_S2C_UserListHeader::kSub,
                               reinterpret_cast<char*>(&outHdr), sizeof(outHdr));
        user->setRoleListReady(false);
        LOG_WARN("角色列表加载失败: conn=%u code=%d", clientConn, hdr->code);
        logLoginFlow(LoginFlowPhase::CHAR_LIST, user->getAccid(), 0, clientConn,
                     hdr->code, "角色列表加载失败");
        return;
    }

    constexpr size_t ENTRY_SIZE = sizeof(Msg_S2C_UserListEntryWire);
    const size_t entryBytes = static_cast<size_t>(hdr->count) * ENTRY_SIZE;
    const size_t expected = sizeof(Msg_REC_ListCharactersRspHeader) + entryBytes;
    if (len < expected)
        return;

    Msg_S2C_UserListHeader outHdr{};
    initClientMsg(outHdr);
    outHdr.code = 0;
    outHdr.count = hdr->count;
    if (entryBytes > (std::numeric_limits<size_t>::max() - sizeof(outHdr)))
    {
        LOG_WARN("角色列表体积异常，拒绝下发: conn=%u count=%u",
                 clientConn, hdr->count);
        user->setRoleListReady(false);
        return;
    }
    const size_t bodyLen = sizeof(outHdr) + entryBytes;
    if (bodyLen > 65535)
    {
        LOG_WARN("角色列表回包过大，拒绝下发: conn=%u count=%u bodyLen=%zu",
                 clientConn, hdr->count, bodyLen);
        user->setRoleListReady(false);
        return;
    }
    std::vector<char> body(bodyLen);
    std::memcpy(body.data(), &outHdr, sizeof(outHdr));
    if (hdr->count > 0)
    {
        std::memcpy(body.data() + sizeof(outHdr),
                    data + sizeof(Msg_REC_ListCharactersRspHeader),
                    entryBytes);
    }
    if (!m_clientServer.SendMsg(clientConn, Msg_S2C_UserListHeader::kModule,
                               Msg_S2C_UserListHeader::kSub,
                               body.data(), static_cast<uint16_t>(bodyLen)))
    {
        LOG_WARN("角色列表下发失败: conn=%u count=%u", clientConn, hdr->count);
        user->setRoleListReady(false);
        return;
    }

    std::unordered_set<uint64_t> roleIds;
    const auto* entries = reinterpret_cast<const Msg_S2C_UserListEntryWire*>(
        body.data() + sizeof(Msg_S2C_UserListHeader));
    for (uint16_t i = 0; i < hdr->count; ++i)
        roleIds.insert(entries[i].userID);
    user->setOwnedRoleIds(std::move(roleIds));
    user->setRoleListReady(true);
    char detail[32];
    snprintf(detail, sizeof(detail), "下发 count=%u", hdr->count);
    logLoginFlow(LoginFlowPhase::CHAR_LIST, user->getAccid(), 0, clientConn, 0, detail);
}

void GatewayServer::onCreateCharacterRsp(ConnID /*fromConn*/, const Msg_REC_CreateCharacterRsp& rsp)
{
    ConnID clientConn = rsp.gatewayConnID;
    auto user = m_userManager.findUser(clientConn);
    if (!user)
    {
        LOG_WARN("创角回包丢弃: 会话不存在 conn=%u code=%d userID=%llu",
                 clientConn, rsp.code, static_cast<unsigned long long>(rsp.userID));
        return;
    }

    Msg_S2C_CreateUserRsp createRsp{};
    initClientMsg(createRsp);
    createRsp.code = rsp.code;
    createRsp.userID = rsp.userID;
    switch (rsp.code)
    {
    case static_cast<int32_t>(CreateCharacterError::OK):
        copyToWire(createRsp.msg, sizeof(createRsp.msg), "创角成功");
        break;
    case static_cast<int32_t>(CreateCharacterError::NAME_EXISTS):
        copyToWire(createRsp.msg, sizeof(createRsp.msg), "角色名已存在");
        break;
    case static_cast<int32_t>(CreateCharacterError::LIMIT_REACHED):
        copyToWire(createRsp.msg, sizeof(createRsp.msg), "角色数量已达上限");
        break;
    case static_cast<int32_t>(CreateCharacterError::INVALID_NAME):
        copyToWire(createRsp.msg, sizeof(createRsp.msg), "角色名非法");
        break;
    case static_cast<int32_t>(CreateCharacterError::INVALID_VOCATION):
        copyToWire(createRsp.msg, sizeof(createRsp.msg), "职业或性别非法");
        break;
    default:
        copyToWire(createRsp.msg, sizeof(createRsp.msg), "创角失败");
        break;
    }
    m_clientServer.SendMsg(clientConn, Msg_S2C_CreateUserRsp::kModule,
                           Msg_S2C_CreateUserRsp::kSub,
                           reinterpret_cast<char*>(&createRsp), sizeof(createRsp));

    if (rsp.code == static_cast<int32_t>(CreateCharacterError::OK) && rsp.userID != 0)
        user->addOwnedRole(rsp.userID);
    if (rsp.code == static_cast<int32_t>(CreateCharacterError::OK))
    {
        user->setRoleListReady(false);
        // 创角已成功下发 S2C_CREATE_USER_RSP；列表刷新失败时仍通知 S2C_USER_LIST code=-1，
        // 客户端可提示重试列表，并凭 CREATE_USER_RSP.userID / ownedRoleIds 选角进世界。
        if (!sendUserListToClient(clientConn, user->getAccid(), user->getZoneId(), true))
        {
            LOG_WARN("创角成功后刷新列表失败: Record 未连接 conn=%u accid=%llu",
                     clientConn, static_cast<unsigned long long>(user->getAccid()));
            logLoginFlow(LoginFlowPhase::CHAR_LIST, user->getAccid(), rsp.userID, clientConn,
                         -1, "创角后列表刷新失败");
        }
    }
    logLoginFlow(LoginFlowPhase::CHAR_CREATE, user->getAccid(), rsp.userID, clientConn,
                 rsp.code, createRsp.msg);
}

void GatewayServer::onClientHeartbeat(ConnID connID, const char* data, uint16_t len)
{
    Msg_S2C_Heartbeat rsp{};
    if (len >= sizeof(Msg_C2S_Heartbeat))
        rsp.seq = reinterpret_cast<const Msg_C2S_Heartbeat*>(data)->seq;
    rsp.serverTime = TimerMgr::NowMs();
    sendClientWire(m_clientServer, connID, rsp);
}

void GatewayServer::onUserLoginRsp(ConnID /*fromConn*/, const Msg_GW_UserLoginRsp& rsp)
{
    ConnID clientConn = rsp.gatewayClientConnID;
    auto user = m_userManager.findUser(clientConn);
    if (!user) return;

    Msg_S2C_LoginRsp loginRsp{};
    initClientMsg(loginRsp);
    loginRsp.code   = rsp.code;
    loginRsp.userID = rsp.userID;
    if (rsp.code == 0)
    {
        user->setClientState(ClientState::IN_WORLD);
        user->setSceneServerId(rsp.sceneServerId);
        copyToWire(loginRsp.msg, sizeof(loginRsp.msg), "进入游戏成功");

        Msg_S2C_EnterGame enter{};
        initClientMsg(enter);
        enter.userID = rsp.userID;
        enter.mapID  = rsp.mapID;
        enter.x      = rsp.x;
        enter.y      = rsp.y;
        enter.z      = rsp.z;
        enter.level  = rsp.level;
        enter.hp     = rsp.hp;
        enter.maxHP  = rsp.maxHP;
        enter.mp     = rsp.mp;
        enter.maxMP  = rsp.maxMP;
        copyToWire(enter.name, sizeof(enter.name), rsp.name);

        m_clientServer.SendMsg(clientConn, Msg_S2C_LoginRsp::kModule, Msg_S2C_LoginRsp::kSub,
                               reinterpret_cast<char*>(&loginRsp), sizeof(loginRsp));
        m_clientServer.SendMsg(clientConn, Msg_S2C_EnterGame::kModule, Msg_S2C_EnterGame::kSub,
                               reinterpret_cast<char*>(&enter), sizeof(enter));
        LOG_INFO("进入游戏成功: connID=%u userID=%llu map=%u sceneServerId=%u",
                 clientConn, rsp.userID, rsp.mapID, rsp.sceneServerId);
    }
    else
    {
        user->setClientState(ClientState::ACCOUNT_OK);
        copyToWire(loginRsp.msg, sizeof(loginRsp.msg), "进入游戏失败");
        m_clientServer.SendMsg(clientConn, Msg_S2C_LoginRsp::kModule, Msg_S2C_LoginRsp::kSub,
                               reinterpret_cast<char*>(&loginRsp), sizeof(loginRsp));
        LOG_WARN("进入游戏失败: conn=%u userID=%llu code=%d",
                 clientConn, rsp.userID, rsp.code);
        logLoginFlow(LoginFlowPhase::CHAR_SELECT, user->getAccid(), rsp.userID, clientConn,
                     rsp.code, "进入游戏失败");
    }
}

void GatewayServer::onSendToClient(ConnID /*fromConn*/, const char* data, uint16_t len)
{
    UnwrappedGwSendToClient unwrapped{};
    if (!unwrapGwSendToClient(data, len, unwrapped))
        return;
    m_clientServer.SendMsg(unwrapped.clientConnId, unwrapped.module, unwrapped.sub,
                           unwrapped.body, unwrapped.bodyLen);
}

void GatewayServer::onKickClient(ConnID /*fromConn*/, const char* data, uint16_t len)
{
    if (len < sizeof(uint32_t)) return;
    uint32_t clientConnID = *reinterpret_cast<const uint32_t*>(data);
    auto user = m_userManager.findUser(clientConnID);
    if (user && user->GetID() != INVALID_USER_ID)
    {
        leaveWorldSession(user, true, "Super踢线");
        clearInWorldUserState(user);
    }
    LOG_INFO("踢下线客户端: connID=%u", clientConnID);
    m_clientServer.Kick(clientConnID);
    m_userManager.removeUser(clientConnID);
}

void GatewayServer::forwardClientMsg(TcpClient& target, ConnID connID,
                                     uint8_t module, uint8_t sub,
                                     const char* data, uint16_t len)
{
    sendGwClientMsg(target, connID, module, sub, data, len);
}

void GatewayServer::checkTimeout()
{
    uint64_t now = TimerMgr::NowMs();
    m_userManager.forEach([&](ConnID connId, GatewayUser& user) {
        if (user.getClientState() == ClientState::CONNECTED &&
            !user.isAuthWarnSent() &&
            now > user.getConnectedAtMs() &&
            now - user.getConnectedAtMs() >= GATEWAY_AUTH_TIMEOUT_MS)
        {
            user.setAuthWarnSent(true);
            logLoginFlow(LoginFlowPhase::GATEWAY_AUTH, 0, 0, connId, -1,
                         "连接后未鉴权超时");
        }
    });
    for (ConnID cid : m_userManager.collectExpiredConnIds(now, 60000))
    {
        auto user = m_userManager.findUser(cid);
        if (user && user->GetID() != INVALID_USER_ID)
        {
            leaveWorldSession(user, true, "心跳超时");
            clearInWorldUserState(user);
        }
        LOG_WARN("客户端心跳超时: connID=%u", cid);
        m_clientServer.Kick(cid);
        m_userManager.removeUser(cid);
    }
}

bool GatewayServer::sendToClient(ConnID connId, uint8_t module, uint8_t sub,
                                 const char* data, uint16_t len)
{
    return m_clientServer.SendMsg(connId, module, sub, data, len);
}

void GatewayServer::registerToSuper()
{
    if (!m_superClient.canSend())
        return;
    Msg_S2S_Register reg{};
    reg.serverType = (uint8_t)SubServerType::GATEWAY;
    reg.serverID   = m_self.id;
    copyToWire(reg.ip, sizeof(reg.ip),
               m_self.ip.empty() ? "127.0.0.1" : m_self.ip.c_str());
    reg.port       = m_clientPort;
    copyToWire(reg.name, sizeof(reg.name), m_self.name.c_str());
    m_superClient.SendMsg((uint16_t)InternalMsgID::S2S_REGISTER_REQ,
                          reinterpret_cast<char*>(&reg), sizeof(reg));
}

void GatewayServer::rescheduleSuperRegisterTimer(uint64_t delayMs)
{
    if (m_superRegisterTimerId != INVALID_TIMER_ID)
    {
        TimerMgr::Instance().Cancel(m_superRegisterTimerId);
        m_superRegisterTimerId = INVALID_TIMER_ID;
    }
    m_superRegisterTimerId = TimerMgr::Instance().Register(delayMs, 0, [this] {
        onSuperRegisterTimerFired();
    });
}

void GatewayServer::scheduleSuperRegister()
{
    if (m_superRegisterPending)
        return;
    m_superRegisterPending = true;
    rescheduleSuperRegisterTimer(500);
}

void GatewayServer::onSuperRegisterTimerFired()
{
    if (!m_superClient.canSend())
    {
        rescheduleSuperRegisterTimer(500);
        return;
    }
    m_superRegisterTimerId = INVALID_TIMER_ID;
    m_superRegisterPending = false;
    registerToSuper();
}

void GatewayServer::tryReconnectSuper()
{
    m_superClient.Disconnect();
    wireTlsClient(m_superClient);
    if (!m_superClient.Connect(m_superIP, m_superPort))
    {
        LOG_WARN("超级服重连失败: %s:%u", m_superIP.c_str(), m_superPort);
        return;
    }
    LOG_INFO("超级服重连已发起: %s:%u", m_superIP.c_str(), m_superPort);
    m_reportedToLogin = false;
    m_superRegisterPending = false;
    scheduleSuperRegister();
}

void GatewayServer::sendHeartbeat()
{
    if (!m_superClient.canSend())
        return;
    Msg_S2S_Heartbeat hb{};
    hb.seq = ++m_hbSeq;
    hb.timestamp = TimerMgr::NowMs();
    hb.onlineCount = static_cast<uint32_t>(m_userManager.getUserCount());
    m_superClient.SendMsg((uint16_t)InternalMsgID::S2S_HEARTBEAT,
                          reinterpret_cast<char*>(&hb), sizeof(hb));
}
