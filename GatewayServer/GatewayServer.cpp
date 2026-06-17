/**
 * @file GatewayServer.cpp
 * @brief GatewayServer 非内联方法实现。
 */

#include "GatewayServer.h"
#include "../sdk/util/ServerBootstrap.h"
#include "../sdk/util/LoginEnterErrorCode.h"
#include "../sdk/util/LoginFlowLog.h"

#include <vector>
#include <cstdio>
#include <cstring>
#include <unordered_set>

namespace {

constexpr uint64_t UPSTREAM_CONNECT_TIMEOUT_MS = 5000;

/** @brief 区内服出站 TcpClient 回调：仅派发消息，不创建客户端会话 */
class GatewayUpstreamCallback : public INetCallback
{
public:
    void OnConnect(ConnID) override {}

    void OnDisconnect(ConnID) override {}

    void OnMessage(ConnID id, uint8_t module, uint8_t sub,
                   const char* data, uint16_t len) override
    {
        MsgDispatcher::Instance().Dispatch(id, module, sub, data, len);
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

    if (!m_clientServer.Start("0.0.0.0", clientPort))
    { LOG_FATAL("客户端监听失败"); return false; }

    m_superClient.Connect(cfg.superIP, (uint16_t)cfg.superPort);
    m_clientPort = clientPort;
    m_zoneId = cfg.zoneId;
    m_gameType = cfg.gameType;

    RegisterHandlers();
    TimerMgr::Instance().Register(500,   0,     [this]{ RegisterToSuper(); });
    TimerMgr::Instance().Register(10000, 10000, [this]{ SendHeartbeat(); });
    TimerMgr::Instance().Register(30000, 30000, [this]{ CheckTimeout(); });
    TimerMgr::Instance().Register(10000, 10000, [this]{ sendLoginGatewayHeartbeat(); });

    LOG_INFO("网关服启动完成（等待 S2S_REGISTER_RSP 后连接上游）");
    return true;
}

void GatewayServer::Run()
{
    while (true)
    {
        m_superClient.Poll(0);
        if (m_upstreamReady)
        {
            m_recordClient.Poll(0);
            m_sessionClient.Poll(0);
            m_scenePool.pollAll();
        }
        m_clientServer.Poll(5);
        TimerMgr::Instance().Update();
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
        if (user->GetID() != INVALID_USER_ID && m_upstreamReady)
        {
            UserID uid = user->GetID();
            SceneClient* scene = m_scenePool.clientFor(user->getSceneServerId());
            if (scene)
                scene->sendMsg(static_cast<uint16_t>(InternalMsgID::SCE_USER_LEAVE),
                               reinterpret_cast<const char*>(&uid), sizeof(UserID));
        }
        m_userManager.removeUser(id);
    }
}

void GatewayServer::OnMessage(ConnID id, uint8_t module, uint8_t sub,
                              const char* data, uint16_t len)
{
    if (m_userManager.findUser(id))
        HandleClientMsg(id, module, sub, data, len);
    else
        MsgDispatcher::Instance().Dispatch(id, module, sub, data, len);
}

void GatewayServer::RegisterHandlers()
{
    auto& d = MsgDispatcher::Instance();
    d.Register((uint16_t)InternalMsgID::GW_SEND_TO_CLIENT,
        [this](uint32_t c, const char* d, uint16_t l){ OnSendToClient(c, d, l); });
    d.Register((uint16_t)InternalMsgID::GW_KICK_CLIENT,
        [this](uint32_t c, const char* d, uint16_t l){ OnKickClient(c, d, l); });
    d.Register((uint16_t)InternalMsgID::REC_LOGIN_VERIFY_RSP,
        [this](uint32_t c, const char* d, uint16_t l){ OnLoginVerifyRsp(c, d, l); });
    d.Register((uint16_t)InternalMsgID::REC_VALIDATE_TOKEN_RSP,
        [this](uint32_t c, const char* d, uint16_t l){ OnValidateTokenRsp(c, d, l); });
    d.Register((uint16_t)InternalMsgID::REC_LIST_CHARACTERS_RSP,
        [this](uint32_t c, const char* d, uint16_t l){ OnListCharactersRsp(c, d, l); });
    d.Register((uint16_t)InternalMsgID::REC_CREATE_CHARACTER_RSP,
        [this](uint32_t c, const char* d, uint16_t l){ OnCreateCharacterRsp(c, d, l); });
    d.Register((uint16_t)InternalMsgID::GW_USER_LOGIN_RSP,
        [this](uint32_t c, const char* d, uint16_t l){ OnUserLoginRsp(c, d, l); });
    d.Register((uint16_t)InternalMsgID::S2S_REGISTER_RSP,
        [this](uint32_t c, const char* d, uint16_t l){ onSuperRegisterRsp(c, d, l); });
    d.Register(static_cast<uint16_t>(InternalMsgID::SS_LOGIN_GATEWAY_WRAP_RSP),
        [this](uint32_t c, const char* d, uint16_t l){ onLoginGatewayWrapRsp(c, d, l); });
}

void GatewayServer::onSuperRegisterRsp(ConnID /*fromConn*/, const char* /*data*/, uint16_t /*len*/)
{
    if (m_upstreamReady)
        return;
    LOG_INFO("收到 S2S_REGISTER_RSP，开始建立上游连接");
    setupUpstreamClients();
}

void GatewayServer::setupUpstreamClients()
{
    if (m_upstreamReady)
        return;

    if (const ServerEntry* rec = m_serverList.findFirst(SubServerType::RECORD))
        m_recordClient.Connect(rec->ip, rec->port);
    else
        LOG_WARN("服务器列表缺少 RECORD 条目");

    if (const ServerEntry* ses = m_serverList.findFirst(SubServerType::SESSION))
        m_sessionClient.Connect(ses->ip, ses->port);
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
        if (m_recordClient.IsConnected() && m_sessionClient.IsConnected() &&
            m_scenePool.hasAnyConnected())
        {
            return;
        }
    }
    LOG_WARN("网关上游连接超时（可能仅部分连通）");
}

void GatewayServer::reportGatewayToSuper()
{
    if (!m_superClient.IsConnected())
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
    if (!m_upstreamReady || !m_superClient.IsConnected())
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

void GatewayServer::onLoginGatewayWrapRsp(ConnID /*fromConn*/, const char* data, uint16_t len)
{
    if (len < sizeof(Msg_SS_LoginGatewayWrapRsp))
        return;
    const auto* rsp = reinterpret_cast<const Msg_SS_LoginGatewayWrapRsp*>(data);
    if (rsp->body.code == 0)
    {
        m_reportedToLogin = true;
        LOG_INFO("登录网关注册回包成功: gatewayId=%u", rsp->body.gatewayServerId);
    }
    else
    {
        LOG_WARN("登录网关注册回包失败: code=%d gatewayId=%u",
                 rsp->body.code, rsp->body.gatewayServerId);
    }
}

void GatewayServer::HandleClientMsg(ConnID connID, uint8_t module, uint8_t sub,
                                    const char* data, uint16_t len)
{
    auto user = m_userManager.findUser(connID);
    if (!user) return;
    user->touchHeartbeat();

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
        if (module == static_cast<uint8_t>(ClientModule::LOGIN) && sub == 0x0D)
        {
            if (user->getClientState() == ClientState::CONNECTED)
                OnGatewayAuth(connID, data, len);
        }
        else if (module == static_cast<uint8_t>(ClientModule::LOGIN) && sub == 0x03)
        {
            if (user->getClientState() == ClientState::CONNECTED)
                OnClientRegister(connID, data, len);
        }
        else if (module == static_cast<uint8_t>(ClientModule::LOGIN) && sub == 0x05)
        {
            if (user->getClientState() == ClientState::ACCOUNT_OK)
                OnSelectUser(connID, data, len);
        }
        else if (module == static_cast<uint8_t>(ClientModule::LOGIN) && sub == 0x07)
        {
            if (user->getClientState() == ClientState::ACCOUNT_OK)
                OnCreateUser(connID, data, len);
        }
        else if (module == static_cast<uint8_t>(ClientModule::LOGIN) && sub == 0x01)
        {
            if (user->getClientState() == ClientState::CONNECTED)
                OnClientLogin(connID, data, len);
            else
                sendClientError(connID, ValidateResult::BAD_STATE);
        }
        else if (module == static_cast<uint8_t>(ClientModule::SYSTEM) && sub == 0x01)
        {
            OnClientHeartbeat(connID, data, len);
        }
        break;
    case ClientForwardTarget::SCENE:
        if (user->getClientState() == ClientState::IN_WORLD)
        {
            if (!m_upstreamReady)
            {
                sendClientError(connID, ValidateResult::BAD_STATE);
                break;
            }
            SceneClient* scene = m_scenePool.clientFor(user->getSceneServerId());
            if (scene && scene->forwardClientMsg(connID, module, sub, data, len))
                break;
            sendClientError(connID, ValidateResult::BAD_STATE);
        }
        else
            sendClientError(connID, ValidateResult::BAD_STATE);
        break;
    case ClientForwardTarget::SESSION:
        if (user->getClientState() == ClientState::IN_WORLD)
        {
            if (!m_upstreamReady)
                sendClientError(connID, ValidateResult::BAD_STATE);
            else
                forwardClientMsg(m_sessionClient, connID, module, sub, data, len);
        }
        else
            sendClientError(connID, ValidateResult::BAD_STATE);
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
    const char* text = "Request rejected";
    switch (vr)
    {
    case ValidateResult::UNKNOWN_MSG:  text = "Unknown message"; break;
    case ValidateResult::BAD_LENGTH:   text = "Invalid packet length"; break;
    case ValidateResult::BAD_STATE:    text = "Invalid client state"; break;
    case ValidateResult::BAD_PAYLOAD:  text = "Invalid payload"; break;
    case ValidateResult::RATE_LIMITED: text = "Rate limited"; break;
    default: break;
    }
    copyToWire(err.msg, sizeof(err.msg), text);
    m_clientServer.SendMsg(connID,
                           static_cast<uint8_t>(ClientModule::SYSTEM), 0x05,
                           reinterpret_cast<char*>(&err), sizeof(err));
}

void GatewayServer::OnClientLogin(ConnID connID, const char* /*data*/, uint16_t /*len*/)
{
    Msg_S2C_LoginRsp loginRsp{};
    loginRsp.code = 1;
    copyToWire(loginRsp.msg, sizeof(loginRsp.msg),
               "请先在 LoginServer 登录并携带 loginToken 连接 Gateway");
    m_clientServer.SendMsg(connID, (uint16_t)ClientMsgID::S2C_LOGIN_RSP,
                           reinterpret_cast<char*>(&loginRsp), sizeof(loginRsp));
    LOG_WARN("客户端直连 Gateway 登录已废弃: conn=%u", connID);
}

void GatewayServer::OnClientRegister(ConnID connID, const char* /*data*/, uint16_t /*len*/)
{
    Msg_S2C_RegisterRsp rsp{};
    rsp.code = -1;
    copyToWire(rsp.msg, sizeof(rsp.msg), "请先连接 LoginServer 注册账号");
    m_clientServer.SendMsg(connID, (uint16_t)ClientMsgID::S2C_REGISTER_RSP,
                           reinterpret_cast<char*>(&rsp), sizeof(rsp));
    LOG_WARN("客户端直连 Gateway 注册已废弃: conn=%u", connID);
}

void GatewayServer::OnGatewayAuth(ConnID connID, const char* data, uint16_t len)
{
    if (!m_upstreamReady || !m_recordClient.IsConnected())
    {
        sendClientError(connID, ValidateResult::BAD_STATE);
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

void GatewayServer::OnSelectUser(ConnID connID, const char* data, uint16_t len)
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

void GatewayServer::OnCreateUser(ConnID connID, const char* data, uint16_t len)
{
    if (len < sizeof(Msg_C2S_CreateUserReq))
        return;
    if (!m_recordClient.IsConnected())
    {
        Msg_S2C_CreateUserRsp createRsp{};
        createRsp.code = static_cast<int32_t>(CreateCharacterError::SYSTEM_ERROR);
        copyToWire(createRsp.msg, sizeof(createRsp.msg), "创角服务不可用");
        m_clientServer.SendMsg(connID, (uint16_t)ClientMsgID::S2C_CREATE_USER_RSP,
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

void GatewayServer::sendUserListToClient(ConnID clientConn, uint64_t accid, uint32_t zoneId)
{
    if (!m_recordClient.IsConnected())
    {
        LOG_WARN("请求角色列表失败: Record 未连接 conn=%u accid=%llu",
                 clientConn, static_cast<unsigned long long>(accid));
        return;
    }
    Msg_REC_ListCharactersReq listReq{};
    listReq.accid = accid;
    listReq.zoneId = zoneId;
    listReq.gatewayConnID = clientConn;
    m_recordClient.SendMsg(static_cast<uint16_t>(InternalMsgID::REC_LIST_CHARACTERS_REQ),
                           reinterpret_cast<char*>(&listReq), sizeof(listReq));
}

void GatewayServer::OnValidateTokenRsp(ConnID /*fromConn*/, const char* data, uint16_t len)
{
    if (len < sizeof(Msg_REC_ValidateTokenRsp))
        return;
    const auto* rsp = reinterpret_cast<const Msg_REC_ValidateTokenRsp*>(data);
    ConnID clientConn = rsp->gatewayConnID;
    auto user = m_userManager.findUser(clientConn);
    if (!user)
        return;

    if (rsp->code != 0)
    {
        Msg_S2C_LoginRsp loginRsp{};
        loginRsp.code = 1;
        copyToWire(loginRsp.msg, sizeof(loginRsp.msg), "登录票据无效或已过期");
        user->setClientState(ClientState::CONNECTED);
        user->setAccid(0);
        user->setRoleListReady(false);
        m_clientServer.SendMsg(clientConn, (uint16_t)ClientMsgID::S2C_LOGIN_RSP,
                               reinterpret_cast<char*>(&loginRsp), sizeof(loginRsp));
        LOG_WARN("Gateway 票据鉴权失败: conn=%u", clientConn);
        logLoginFlow(LoginFlowPhase::GATEWAY_AUTH, 0, 0, clientConn, rsp->code, nullptr);
        m_clientServer.Kick(clientConn);
        m_userManager.removeUser(clientConn);
        return;
    }

    user->setAccid(rsp->accid);
    user->setClientState(ClientState::ACCOUNT_OK);
    user->setRoleListReady(false);

    Msg_S2C_LoginRsp loginRsp{};
    loginRsp.code = 0;
    loginRsp.userID = 0;
    loginRsp.accid = rsp->accid;
    loginRsp.loginToken[0] = '\0';
    loginRsp.tokenExpireMs = 0;
    copyToWire(loginRsp.msg, sizeof(loginRsp.msg), "网关鉴权成功");
    m_clientServer.SendMsg(clientConn, (uint16_t)ClientMsgID::S2C_LOGIN_RSP,
                           reinterpret_cast<char*>(&loginRsp), sizeof(loginRsp));

    sendUserListToClient(clientConn, rsp->accid, user->getZoneId());
    logLoginFlow(LoginFlowPhase::GATEWAY_AUTH, rsp->accid, 0, clientConn, 0, "鉴权成功");
}

void GatewayServer::OnListCharactersRsp(ConnID /*fromConn*/, const char* data, uint16_t len)
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
        outHdr.code = hdr->code;
        outHdr.count = 0;
        m_clientServer.SendMsg(clientConn, (uint16_t)ClientMsgID::S2C_USER_LIST,
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
    m_clientServer.SendMsg(clientConn, (uint16_t)ClientMsgID::S2C_USER_LIST,
                           body.data(), static_cast<uint16_t>(bodyLen));

    std::unordered_set<uint64_t> roleIds;
    const auto* entries = reinterpret_cast<const Msg_S2C_UserListEntryWire*>(
        body.data() + sizeof(Msg_S2C_UserListHeader));
    for (uint16_t i = 0; i < hdr->count; ++i)
        roleIds.insert(entries[i].userID);
    user->setOwnedRoleIds(std::move(roleIds));
    user->setRoleListReady(true);
    logLoginFlow(LoginFlowPhase::CHAR_LIST, 0, 0, clientConn, 0, nullptr);
}

void GatewayServer::OnCreateCharacterRsp(ConnID /*fromConn*/, const char* data, uint16_t len)
{
    if (len < sizeof(Msg_REC_CreateCharacterRsp))
        return;
    const auto* rsp = reinterpret_cast<const Msg_REC_CreateCharacterRsp*>(data);
    ConnID clientConn = rsp->gatewayConnID;
    auto user = m_userManager.findUser(clientConn);
    if (!user)
    {
        LOG_WARN("创角回包丢弃: 会话不存在 conn=%u code=%d userID=%llu",
                 clientConn, rsp->code, static_cast<unsigned long long>(rsp->userID));
        return;
    }

    Msg_S2C_CreateUserRsp createRsp{};
    createRsp.code = rsp->code;
    createRsp.userID = rsp->userID;
    switch (rsp->code)
    {
    case static_cast<int32_t>(CreateCharacterError::OK):
        copyToWire(createRsp.msg, sizeof(createRsp.msg), "Create OK");
        break;
    case static_cast<int32_t>(CreateCharacterError::NAME_EXISTS):
        copyToWire(createRsp.msg, sizeof(createRsp.msg), "Name exists");
        break;
    case static_cast<int32_t>(CreateCharacterError::LIMIT_REACHED):
        copyToWire(createRsp.msg, sizeof(createRsp.msg), "Character limit");
        break;
    case static_cast<int32_t>(CreateCharacterError::INVALID_NAME):
        copyToWire(createRsp.msg, sizeof(createRsp.msg), "Invalid name");
        break;
    default:
        copyToWire(createRsp.msg, sizeof(createRsp.msg), "Create failed");
        break;
    }
    m_clientServer.SendMsg(clientConn, (uint16_t)ClientMsgID::S2C_CREATE_USER_RSP,
                           reinterpret_cast<char*>(&createRsp), sizeof(createRsp));

    if (rsp->code == static_cast<int32_t>(CreateCharacterError::OK) && rsp->userID != 0)
        user->addOwnedRole(rsp->userID);
    if (rsp->code == static_cast<int32_t>(CreateCharacterError::OK))
        sendUserListToClient(clientConn, user->getAccid(), user->getZoneId());
    logLoginFlow(LoginFlowPhase::CHAR_CREATE, user->getAccid(), rsp->userID, clientConn,
                 rsp->code, createRsp.msg);
}

void GatewayServer::OnClientHeartbeat(ConnID connID, const char* data, uint16_t len)
{
    Msg_S2C_Heartbeat rsp{};
    if (len >= sizeof(Msg_C2S_Heartbeat))
        rsp.seq = reinterpret_cast<const Msg_C2S_Heartbeat*>(data)->seq;
    rsp.serverTime = TimerMgr::NowMs();
    m_clientServer.SendMsg(connID, (uint16_t)ClientMsgID::S2C_HEARTBEAT,
                           reinterpret_cast<char*>(&rsp), sizeof(rsp));
}

void GatewayServer::OnLoginVerifyRsp(ConnID /*fromConn*/, const char* /*data*/, uint16_t /*len*/)
{
    /* 旧 REC_LOGIN_VERIFY 直连登录路径已废弃 */
}

void GatewayServer::OnUserLoginRsp(ConnID /*fromConn*/, const char* data, uint16_t len)
{
    if (len < sizeof(Msg_GW_UserLoginRsp)) return;
    const auto* rsp = reinterpret_cast<const Msg_GW_UserLoginRsp*>(data);
    ConnID clientConn = rsp->gatewayClientConnID;
    auto user = m_userManager.findUser(clientConn);
    if (!user) return;

    Msg_S2C_LoginRsp loginRsp{};
    loginRsp.code   = rsp->code;
    loginRsp.userID = rsp->userID;
    if (rsp->code == 0)
    {
        user->setClientState(ClientState::IN_WORLD);
        user->setSceneServerId(rsp->sceneServerId);
        copyToWire(loginRsp.msg, sizeof(loginRsp.msg), "Login OK");

        Msg_S2C_EnterGame enter{};
        enter.userID = rsp->userID;
        enter.mapID  = rsp->mapID;
        enter.x      = rsp->x;
        enter.y      = rsp->y;
        enter.z      = rsp->z;
        enter.level  = rsp->level;
        enter.hp     = rsp->hp;
        enter.maxHP  = rsp->maxHP;
        enter.mp     = rsp->mp;
        enter.maxMP  = rsp->maxMP;
        snprintf(enter.name, sizeof(enter.name), "%s", rsp->name);

        m_clientServer.SendMsg(clientConn, (uint16_t)ClientMsgID::S2C_LOGIN_RSP,
                               reinterpret_cast<char*>(&loginRsp), sizeof(loginRsp));
        m_clientServer.SendMsg(clientConn, (uint16_t)ClientMsgID::S2C_ENTER_GAME,
                               reinterpret_cast<char*>(&enter), sizeof(enter));
        LOG_INFO("进入游戏成功: connID=%u userID=%llu map=%u sceneServerId=%u",
                 clientConn, rsp->userID, rsp->mapID, rsp->sceneServerId);
    }
    else
    {
        user->setClientState(ClientState::ACCOUNT_OK);
        copyToWire(loginRsp.msg, sizeof(loginRsp.msg), "Enter game failed");
        m_clientServer.SendMsg(clientConn, (uint16_t)ClientMsgID::S2C_LOGIN_RSP,
                               reinterpret_cast<char*>(&loginRsp), sizeof(loginRsp));
        LOG_WARN("进入游戏失败: conn=%u userID=%llu code=%d",
                 clientConn, rsp->userID, rsp->code);
    }
}

void GatewayServer::OnSendToClient(ConnID /*fromConn*/, const char* data, uint16_t len)
{
    if (len < sizeof(Msg_GW_SendToClient)) return;
    const auto* hdr = reinterpret_cast<const Msg_GW_SendToClient*>(data);
    const char* body = data + sizeof(Msg_GW_SendToClient);
    if (len < sizeof(Msg_GW_SendToClient) + hdr->dataLen) return;
    m_clientServer.SendMsg(hdr->clientConnID, hdr->module, hdr->sub,
                           body, hdr->dataLen);
}

void GatewayServer::OnKickClient(ConnID /*fromConn*/, const char* data, uint16_t len)
{
    if (len < sizeof(uint32_t)) return;
    uint32_t clientConnID = *reinterpret_cast<const uint32_t*>(data);
    LOG_INFO("踢下线客户端: connID=%u", clientConnID);
    m_clientServer.Kick(clientConnID);
    m_userManager.removeUser(clientConnID);
}

void GatewayServer::forwardClientMsg(TcpClient& target, ConnID connID,
                                     uint8_t module, uint8_t sub,
                                     const char* data, uint16_t len)
{
    std::vector<char> buf(sizeof(Msg_GW_ClientMsg) + len);
    auto* hdr = reinterpret_cast<Msg_GW_ClientMsg*>(buf.data());
    hdr->clientConnID = connID;
    hdr->module       = module;
    hdr->sub          = sub;
    hdr->dataLen      = len;
    if (len > 0)
        memcpy(buf.data() + sizeof(Msg_GW_ClientMsg), data, len);
    target.SendMsg(static_cast<uint16_t>(InternalMsgID::GW_CLIENT_MSG),
                   buf.data(), static_cast<uint16_t>(buf.size()));
}

void GatewayServer::CheckTimeout()
{
    uint64_t now = TimerMgr::NowMs();
    for (ConnID cid : m_userManager.collectExpiredConnIds(now, 60000))
    {
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

void GatewayServer::RegisterToSuper()
{
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

void GatewayServer::SendHeartbeat()
{
    Msg_S2S_Heartbeat hb{};
    hb.seq = ++m_hbSeq;
    hb.timestamp = TimerMgr::NowMs();
    hb.onlineCount = static_cast<uint32_t>(m_userManager.getUserCount());
    m_superClient.SendMsg((uint16_t)InternalMsgID::S2S_HEARTBEAT,
                          reinterpret_cast<char*>(&hb), sizeof(hb));
}
