/**
 * @file GatewayServer.cpp
 * @brief GatewayServer 非内联方法实现。
 */

#include "GatewayServer.h"

#include <cstdio>
#include <cstring>
#include <vector>

GatewayServer::GatewayServer()
    : m_clientServer(this)
    , m_innerServer(this)
    , m_superClient(this)
    , m_recordClient(this)
    , m_sceneClient(this)
    , m_sessionClient(this)
{
}

bool GatewayServer::Init(uint16_t clientPort, uint16_t innerPort,
                         const ServerConfig& cfg)
{
    Logger::Instance().SetServerName("GatewayServer");
    LOG_INFO("GatewayServer starting: clientPort=%d innerPort=%d", clientPort, innerPort);

    if (!m_clientServer.Start("0.0.0.0", clientPort))
    { LOG_FATAL("Client listen failed"); return false; }
    if (!m_innerServer.Start("0.0.0.0", innerPort))
    { LOG_FATAL("Inner listen failed"); return false; }

    m_superClient.Connect(cfg.superIP,   (uint16_t)cfg.superPort);
    m_recordClient.Connect("127.0.0.1",  (uint16_t)cfg.recordPort);
    m_sceneClient.Connect("127.0.0.1",   (uint16_t)cfg.scenePort);
    m_sessionClient.Connect("127.0.0.1", (uint16_t)cfg.sessionPort);

    m_clientPort = clientPort;
    m_innerPort  = innerPort;

    RegisterHandlers();
    TimerMgr::Instance().Register(500,   0,     [this]{ RegisterToSuper(); });
    TimerMgr::Instance().Register(10000, 10000, [this]{ SendHeartbeat(); });
    TimerMgr::Instance().Register(30000, 30000, [this]{ CheckTimeout(); });

    LOG_INFO("GatewayServer started.");
    return true;
}

void GatewayServer::Run()
{
    while (true)
    {
        m_superClient.Poll(0);
        m_recordClient.Poll(0);
        m_sceneClient.Poll(0);
        m_sessionClient.Poll(0);
        m_clientServer.Poll(5);
        m_innerServer.Poll(5);
        TimerMgr::Instance().Update();
    }
}

void GatewayServer::OnConnect(ConnID id)
{
    if (id < 100000)
    {
        m_userManager.addUser(id);
        LOG_INFO("Client connected: connID=%u", id);
    }
    else
    {
        LOG_INFO("InnerServer connected: connID=%u", id);
    }
}

void GatewayServer::OnDisconnect(ConnID id)
{
    auto user = m_userManager.findUser(id);
    if (user)
    {
        if (user->GetID() != INVALID_USER_ID)
        {
            UserID uid = user->GetID();
            m_sceneClient.SendMsg((uint16_t)InternalMsgID::SCE_USER_LEAVE,
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
            forwardClientMsg(m_sceneClient, connID, module, sub, data, len);
        else
            sendClientError(connID, ValidateResult::BAD_STATE);
        break;
    case ClientForwardTarget::SESSION:
        if (user->getClientState() == ClientState::LOGGED_IN)
            forwardClientMsg(m_sessionClient, connID, module, sub, data, len);
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
        LOG_INFO("EnterGame: connID=%u userID=%llu map=%u",
                 clientConn, rsp->userID, rsp->mapID);
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

void GatewayServer::RegisterToSuper()
{
    Msg_S2S_Register reg{};
    reg.serverType = (uint8_t)SubServerType::GATEWAY;
    reg.serverID   = 1;
    copyToWire(reg.ip, sizeof(reg.ip), "127.0.0.1");
    reg.port       = m_clientPort;
    m_superClient.SendMsg((uint16_t)InternalMsgID::S2S_REGISTER_REQ,
                          reinterpret_cast<char*>(&reg), sizeof(reg));
}

void GatewayServer::SendHeartbeat()
{
    Msg_S2S_Heartbeat hb{}; hb.seq = ++m_hbSeq; hb.timestamp = TimerMgr::NowMs();
    m_superClient.SendMsg((uint16_t)InternalMsgID::S2S_HEARTBEAT,
                          reinterpret_cast<char*>(&hb), sizeof(hb));
}
