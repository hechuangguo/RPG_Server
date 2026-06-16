/**
 * @file GatewayServer.cpp
 * @brief GatewayServer 非内联方法实现。
 */

#include "GatewayServer.h"
#include "../sdk/util/ServerBootstrap.h"

#include <cstdio>
#include <cstring>
#include <vector>

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
    LOG_INFO("GatewayServer starting: clientPort=%d", clientPort);

    m_serverList = list;
    if (const ServerEntry* self = list.find(SubServerType::GATEWAY, selfId))
        m_self = *self;

    if (!m_clientServer.Start("0.0.0.0", clientPort))
    { LOG_FATAL("Client listen failed"); return false; }

    m_superClient.Connect(cfg.superIP, (uint16_t)cfg.superPort);
    m_clientPort = clientPort;
    m_zoneId = cfg.zoneId;
    m_gameType = cfg.gameType;

    RegisterHandlers();
    TimerMgr::Instance().Register(500,   0,     [this]{ RegisterToSuper(); });
    TimerMgr::Instance().Register(10000, 10000, [this]{ SendHeartbeat(); });
    TimerMgr::Instance().Register(30000, 30000, [this]{ CheckTimeout(); });
    TimerMgr::Instance().Register(10000, 10000, [this]{ sendLoginGatewayHeartbeat(); });

    LOG_INFO("GatewayServer started (awaiting S2S_REGISTER_RSP for upstream).");
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
    LOG_INFO("Client connected: connID=%u", id);
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
    LOG_INFO("S2S_REGISTER_RSP received, setting up upstream clients");
    setupUpstreamClients();
}

void GatewayServer::setupUpstreamClients()
{
    if (m_upstreamReady)
        return;

    if (const ServerEntry* rec = m_serverList.findFirst(SubServerType::RECORD))
        m_recordClient.Connect(rec->ip, rec->port);
    else
        LOG_WARN("ServerList missing RECORD entry");

    if (const ServerEntry* ses = m_serverList.findFirst(SubServerType::SESSION))
        m_sessionClient.Connect(ses->ip, ses->port);
    else
        LOG_WARN("ServerList missing SESSION entry");

    if (!m_scenePool.connectAll(m_serverList))
        LOG_WARN("No SCENE connections initiated");

    pollUpstreamUntilReady();

    m_upstreamReady = true;
    LOG_INFO("Gateway upstream ready: record=%d session=%d scene=%d",
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
    LOG_WARN("Gateway upstream connect timeout (partial connections may exist)");
}

void GatewayServer::reportGatewayToSuper()
{
    if (!m_superClient.IsConnected())
    {
        LOG_WARN("SuperServer not connected; skip gateway register");
        return;
    }

    Msg_SS_LoginGatewayWrap wrap{};
    wrap.gatewayConnID = 0;
    wrap.body.gatewayServerId = m_self.id;
    wrap.body.zoneId = m_zoneId;
    wrap.body.gameType = m_gameType;
    wrap.body.port = m_clientPort;
    copyToWire(wrap.body.ip, sizeof(wrap.body.ip),
               m_self.ip.empty() ? "127.0.0.1" : m_self.ip.c_str());
    copyToWire(wrap.body.name, sizeof(wrap.body.name), m_self.name.c_str());
    copyToWire(wrap.body.zoneName, sizeof(wrap.body.zoneName), "game-zone");

    m_superClient.SendMsg(static_cast<uint16_t>(InternalMsgID::SS_LOGIN_GATEWAY_WRAP_REQ),
                          reinterpret_cast<char*>(&wrap), sizeof(wrap));
    LOG_INFO("SS_LOGIN_GATEWAY_WRAP sent: id=%u %s:%u",
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
        LOG_INFO("SS_LOGIN_GATEWAY_WRAP_RSP ok: gatewayId=%u", rsp->body.gatewayServerId);
    }
    else
    {
        LOG_WARN("SS_LOGIN_GATEWAY_WRAP_RSP failed: code=%d gatewayId=%u",
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
        LOG_WARN("ClientMsg rejected: conn=%u mod=0x%02X sub=0x%02X vr=%u",
                 connID, module, sub, static_cast<unsigned>(vr));
        return;
    }

    const ClientForwardTarget target = ClientMsgRouter::resolve(module, sub);
    switch (target)
    {
    case ClientForwardTarget::LOCAL:
        if (module == static_cast<uint8_t>(ClientModule::LOGIN) && sub == 0x01)
        {
            if (user->getClientState() == ClientState::CONNECTED)
                OnClientLogin(connID, data, len);
        }
        else if (module == static_cast<uint8_t>(ClientModule::SYSTEM) && sub == 0x01)
        {
            OnClientHeartbeat(connID, data, len);
        }
        break;
    case ClientForwardTarget::SCENE:
        if (user->getClientState() == ClientState::LOGGED_IN)
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
        if (user->getClientState() == ClientState::LOGGED_IN)
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

void GatewayServer::OnClientLogin(ConnID connID, const char* data, uint16_t len)
{
    if (!m_upstreamReady || !m_recordClient.IsConnected())
    {
        sendClientError(connID, ValidateResult::BAD_STATE);
        return;
    }
    if (len < sizeof(Msg_C2S_LoginReq)) return;
    const auto* req = reinterpret_cast<const Msg_C2S_LoginReq*>(data);
    m_userManager.getUser(connID).setClientState(ClientState::LOGGING);

    Msg_REC_LoginVerifyReq verifyReq{};
    copyToWire(verifyReq.account, sizeof(verifyReq.account), req->account);
    copyToWire(verifyReq.password, sizeof(verifyReq.password), req->password);
    verifyReq.gatewayConnID = connID;
    m_recordClient.SendMsg((uint16_t)InternalMsgID::REC_LOGIN_VERIFY_REQ,
                           reinterpret_cast<char*>(&verifyReq), sizeof(verifyReq));
    LOG_INFO("ClientLogin: account=%s connID=%u", req->account, connID);
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

void GatewayServer::OnLoginVerifyRsp(ConnID /*fromConn*/, const char* data, uint16_t len)
{
    if (len < sizeof(Msg_REC_LoginVerifyRsp)) return;
    const auto* rsp = reinterpret_cast<const Msg_REC_LoginVerifyRsp*>(data);
    ConnID clientConn = rsp->gatewayConnID;
    auto user = m_userManager.findUser(clientConn);
    if (!user) return;

    if (rsp->code != 0)
    {
        Msg_S2C_LoginRsp loginRsp{};
        loginRsp.code = rsp->code;
        copyToWire(loginRsp.msg, sizeof(loginRsp.msg), "Account or password error");
        user->setClientState(ClientState::CONNECTED);
        m_clientServer.SendMsg(clientConn, (uint16_t)ClientMsgID::S2C_LOGIN_RSP,
                               reinterpret_cast<char*>(&loginRsp), sizeof(loginRsp));
        LOG_WARN("LoginFail: connID=%u code=%d", clientConn, rsp->code);
        return;
    }

    user->setUserId(rsp->userID);
    m_superClient.SendMsg((uint16_t)InternalMsgID::GW_USER_LOGIN_REQ,
                          data, len);
    LOG_INFO("LoginVerifyOK, forwarding to Super: connID=%u userID=%llu",
             clientConn, rsp->userID);
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
        user->setClientState(ClientState::LOGGED_IN);
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
        LOG_INFO("EnterGame: connID=%u userID=%llu map=%u sceneServerId=%u",
                 clientConn, rsp->userID, rsp->mapID, rsp->sceneServerId);
    }
    else
    {
        user->setClientState(ClientState::CONNECTED);
        copyToWire(loginRsp.msg, sizeof(loginRsp.msg), "Enter game failed");
        m_clientServer.SendMsg(clientConn, (uint16_t)ClientMsgID::S2C_LOGIN_RSP,
                               reinterpret_cast<char*>(&loginRsp), sizeof(loginRsp));
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
    LOG_INFO("KickClient: connID=%u", clientConnID);
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
        LOG_WARN("ClientTimeout: connID=%u", cid);
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
