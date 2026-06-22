/**
 * @file GatewayServer.cpp
 * @brief GatewayServer 实现：客户端 Protobuf 登录流程、Record 协作与 Scene 转发
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
#include "../sdk/net/ClientProtoWire.h"
#include "ClientCommon.pb.h"
#include "LoginMsg.pb.h"
#include "SystemMsg.pb.h"

#include <vector>
#include <cstdio>
#include <cstring>
#include <memory>
#include <unordered_set>

namespace {

constexpr uint64_t UPSTREAM_CONNECT_TIMEOUT_MS = 5000;
constexpr uint64_t GATEWAY_AUTH_WAIT_MS = 3000;
constexpr uint64_t GATEWAY_AUTH_TIMEOUT_MS = 10000;

constexpr uint8_t kLoginModule  = static_cast<uint8_t>(rpg::client::LOGIN);
constexpr uint8_t kSystemModule = static_cast<uint8_t>(rpg::client::SYSTEM);

rpg::login::S2CUserList buildUserListProto(int32_t code,
                                           const Msg_REC_CharacterEntryWire* entries,
                                           uint16_t count)
{
    rpg::login::S2CUserList list;
    list.set_code(code);
    for (uint16_t i = 0; i < count; ++i)
    {
        auto* e = list.add_entries();
        e->set_user_id(entries[i].userID);
        e->set_name(entries[i].name);
        e->set_level(entries[i].level);
        e->set_vocation(entries[i].vocation);
        e->set_sex(entries[i].sex);
    }
    return list;
}

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
    TimerMgr::Instance().Register(5000, 5000, [this]{ upstreamHealthCheck(); });

    LOG_INFO("网关服启动完成（等待 S2S_REGISTER_RSP 后连接上游）");
    return true;
}

void GatewayServer::Run()
{
    while (true)
    {
        tickUpstreamConnect();
        m_clientServer.Poll(5);
        TimerMgr::Instance().Update();
        /** 定时器回调 SendMsg 后统一 Poll 出站，避免同迭代内重复 Poll */
        m_superClient.Poll(0);
        m_recordClient.Poll(0);
        m_sessionClient.Poll(0);
        m_scenePool.pollAll();
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
        m_recordClient.Disconnect();
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

    m_upstreamConnectPending = true;
    m_upstreamReady = false;
    LOG_INFO("网关上游连接已发起（主循环内异步就绪）");
}

void GatewayServer::tickUpstreamConnect()
{
    if (m_upstreamReady || !m_upstreamConnectPending)
        return;

    m_recordClient.Poll(0);
    m_sessionClient.Poll(0);
    m_scenePool.pollAll();
    m_superClient.Poll(0);

    if (!m_recordClient.canSend() || !m_sessionClient.canSend() ||
        !m_scenePool.hasAnyCanSend())
        return;

    m_upstreamConnectPending = false;
    m_upstreamReady = isRecordReady();
    LOG_INFO("网关上游就绪: record=%d session=%d scene=%d upstreamReady=%d",
             m_recordClient.canSend() ? 1 : 0,
             m_sessionClient.canSend() ? 1 : 0,
             m_scenePool.hasAnyCanSend() ? 1 : 0,
             m_upstreamReady ? 1 : 0);

    if (m_upstreamReady && m_superClient.canSend() && !m_reportedToLogin)
        reportGatewayToSuper();
}

bool GatewayServer::isRecordReady() const
{
    return m_recordClient.canSend();
}

void GatewayServer::reconnectRecordClient()
{
    if (isRecordReady())
        return;
    m_recordClient.Disconnect();
    const ServerEntry* rec = m_serverList.findFirst(SubServerType::RECORD);
    if (!rec)
    {
        LOG_WARN("重连 Record 失败: 服务器列表缺少 RECORD 条目");
        return;
    }
    wireTlsClient(m_recordClient);
    if (!m_recordClient.Connect(rec->ip, rec->port))
        LOG_WARN("重连 Record 发起失败: %s:%u", rec->ip.c_str(), rec->port);
}

bool GatewayServer::ensureRecordReady(uint64_t timeoutMs)
{
    if (isRecordReady())
        return true;
    reconnectRecordClient();
    const uint64_t deadline = TimerMgr::NowMs() + timeoutMs;
    while (TimerMgr::NowMs() < deadline)
    {
        m_recordClient.Poll(10);
        m_superClient.Poll(0);
        if (isRecordReady())
        {
            if (!m_upstreamReady)
            {
                m_upstreamReady = true;
                LOG_INFO("Record 上游已恢复（鉴权等待期间）");
                if (m_superClient.canSend() && !m_reportedToLogin)
                    reportGatewayToSuper();
            }
            return true;
        }
    }
    return false;
}

void GatewayServer::upstreamHealthCheck()
{
    if (isRecordReady())
    {
        if (!m_upstreamReady)
        {
            m_upstreamReady = true;
            LOG_INFO("Record 上游已恢复");
            if (m_superClient.canSend() && !m_reportedToLogin)
                reportGatewayToSuper();
        }
        return;
    }
    if (m_upstreamReady)
    {
        LOG_WARN("Record 上游不可用，暂停 Login 网关注册");
        m_upstreamReady = false;
        m_reportedToLogin = false;
    }
    reconnectRecordClient();
    for (int i = 0; i < 5 && !isRecordReady(); ++i)
        m_recordClient.Poll(10);
    if (isRecordReady())
    {
        m_upstreamReady = true;
        LOG_INFO("Record 上游重连成功");
        if (m_superClient.canSend() && !m_reportedToLogin)
            reportGatewayToSuper();
    }
}

void GatewayServer::reportGatewayToSuper()
{
    if (!isRecordReady())
    {
        LOG_WARN("Record 未就绪，跳过网关注册");
        return;
    }
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
    if (!m_upstreamReady || !isRecordReady() || !m_superClient.canSend())
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
        if (ClientMsgValidator::looksLikeLegacyWireV2(module, sub, data, len))
        {
            sendClientLegacyWireError(connID);
            LOG_WARN("客户端消息被拒绝: conn=%u mod=0x%02X sub=0x%02X legacy wire v2",
                     connID, module, sub);
            if (module == kLoginModule &&
                sub == static_cast<uint8_t>(rpg::login::C2S_GATEWAY_AUTH_REQ))
            {
                logLoginFlow(LoginFlowPhase::GATEWAY_AUTH, 0, 0, connID,
                             static_cast<int32_t>(ValidateResult::BAD_PAYLOAD),
                             "legacy wire v2 rejected");
            }
        }
        else
        {
            sendClientError(connID, vr);
            LOG_WARN("客户端消息被拒绝: conn=%u mod=0x%02X sub=0x%02X vr=%u",
                     connID, module, sub, static_cast<unsigned>(vr));
        }
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
    rpg::system::S2CError err;
    err.set_code(ClientMsgValidator::toErrorCode(vr));
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
    err.set_msg(text);
    sendClientProtoModule(m_clientServer, connID, kSystemModule,
                  static_cast<uint8_t>(rpg::system::S2C_ERROR), err);
}

void GatewayServer::sendClientLegacyWireError(ConnID connID)
{
    rpg::system::S2CError err;
    err.set_code(static_cast<int32_t>(rpg::system::GATEWAY_VALIDATE_BAD_PAYLOAD));
    err.set_msg("客户端协议版本过旧，请使用 Protobuf 重发 Gateway 消息");
    sendClientProtoModule(m_clientServer, connID, kSystemModule,
                  static_cast<uint8_t>(rpg::system::S2C_ERROR), err);
}

void GatewayServer::onGatewayAuth(ConnID connID, const char* data, uint16_t len)
{
    if (!ensureRecordReady(GATEWAY_AUTH_WAIT_MS))
    {
        rpg::login::S2CLoginRsp loginRsp;
        loginRsp.set_code(-1);
        loginRsp.set_msg("网关服务初始化中，请稍后重试");
        sendClientProtoModule(m_clientServer, connID, kLoginModule,
                     static_cast<uint8_t>(rpg::login::S2C_LOGIN_RSP), loginRsp);
        logLoginFlow(LoginFlowPhase::GATEWAY_AUTH, 0, 0, connID, -1, "上游未就绪");
        return;
    }
    if (len < 1)
        return;
    rpg::login::C2SGatewayAuthReq protoReq;
    if (!parseProto(data, len, protoReq))
        return;
    auto user = m_userManager.findUser(connID);
    if (!user)
    {
        sendClientError(connID, ValidateResult::BAD_STATE);
        return;
    }
    user->setZoneId(protoReq.zone_id());
    user->setGameType(static_cast<uint8_t>(protoReq.game_type()));
    user->setClientState(ClientState::AUTHING);

    Msg_REC_ValidateTokenReq verifyReq{};
    copyToWire(verifyReq.loginToken, sizeof(verifyReq.loginToken), protoReq.login_token().c_str());
    verifyReq.zoneId = protoReq.zone_id();
    verifyReq.gameType = static_cast<uint8_t>(protoReq.game_type());
    verifyReq.gatewayConnID = connID;
    m_recordClient.SendMsg(static_cast<uint16_t>(InternalMsgID::REC_VALIDATE_TOKEN_REQ),
                           reinterpret_cast<char*>(&verifyReq), sizeof(verifyReq));
    LOG_INFO("Gateway 票据鉴权: account=%s conn=%u zone=%u", protoReq.account().c_str(), connID,
             protoReq.zone_id());
    logLoginFlow(LoginFlowPhase::GATEWAY_AUTH, 0, 0, connID, 0, "发起 token 校验");
}

void GatewayServer::onSelectUser(ConnID connID, const char* data, uint16_t len)
{
    rpg::login::C2SSelectUserReq protoReq;
    if (!parseProto(data, len, protoReq))
        return;
    auto user = m_userManager.findUser(connID);
    if (!user)
        return;

    if (user->getAccid() == 0)
    {
        sendClientError(connID, ValidateResult::BAD_STATE);
        logLoginFlow(LoginFlowPhase::CHAR_SELECT, 0, protoReq.user_id(), connID,
                     static_cast<int32_t>(ValidateResult::BAD_STATE), "账号未鉴权");
        return;
    }

    if (!user->isRoleListReady())
    {
        sendClientError(connID, ValidateResult::BAD_STATE);
        logLoginFlow(LoginFlowPhase::CHAR_SELECT, user->getAccid(), protoReq.user_id(), connID,
                     static_cast<int32_t>(ValidateResult::BAD_STATE), "角色列表未就绪");
        return;
    }

    if (!user->ownsRole(protoReq.user_id()))
    {
        sendClientError(connID, ValidateResult::BAD_PAYLOAD);
        logLoginFlow(LoginFlowPhase::CHAR_SELECT, user->getAccid(), protoReq.user_id(), connID,
                     static_cast<int32_t>(ValidateResult::BAD_PAYLOAD), "选角归属校验失败");
        return;
    }

    uint64_t txnId = protoReq.login_txn_id();
    if (txnId == 0)
    {
        if (user->GetID() == protoReq.user_id() && user->getLoginTxnId() != 0)
            txnId = user->getLoginTxnId();
        else
            txnId = (TimerMgr::NowMs() << 16) ^ static_cast<uint64_t>(connID) ^ protoReq.user_id();
    }

    user->setUserId(protoReq.user_id());
    user->setLoginTxnId(txnId);
    user->setClientState(ClientState::ENTERING);

    Msg_GW_UserEnterReq enterReq{};
    enterReq.userID = protoReq.user_id();
    enterReq.gatewayClientConnID = connID;
    enterReq.loginTxnId = txnId;
    m_superClient.SendMsg(static_cast<uint16_t>(InternalMsgID::GW_USER_LOGIN_REQ),
                          reinterpret_cast<char*>(&enterReq), sizeof(enterReq));
    LOG_INFO("选角进世界: conn=%u userID=%llu txn=%llu", connID,
             static_cast<unsigned long long>(protoReq.user_id()),
             static_cast<unsigned long long>(txnId));
    logLoginFlow(LoginFlowPhase::CHAR_SELECT, user->getAccid(), protoReq.user_id(), connID, 0,
                 nullptr, txnId);
}

void GatewayServer::onCreateUser(ConnID connID, const char* data, uint16_t len)
{
    rpg::login::C2SCreateUserReq protoReq;
    if (!parseProto(data, len, protoReq))
        return;
    if (!m_recordClient.IsConnected())
    {
        rpg::login::S2CCreateUserRsp createRsp;
        createRsp.set_code(static_cast<int32_t>(CreateCharacterError::SYSTEM_ERROR));
        createRsp.set_msg("创角服务不可用");
        sendClientProtoModule(m_clientServer, connID, kLoginModule,
                     static_cast<uint8_t>(rpg::login::S2C_CREATE_USER_RSP), createRsp);
        return;
    }
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
    copyToWire(createReq.name, sizeof(createReq.name), protoReq.name().c_str());
    createReq.vocation = static_cast<uint8_t>(protoReq.vocation());
    createReq.sex = static_cast<uint8_t>(protoReq.sex());
    createReq.gatewayConnID = connID;
    m_recordClient.SendMsg(static_cast<uint16_t>(InternalMsgID::REC_CREATE_CHARACTER_REQ),
                           reinterpret_cast<char*>(&createReq), sizeof(createReq));
    logLoginFlow(LoginFlowPhase::CHAR_CREATE, user->getAccid(), 0, connID, 0,
                 protoReq.name().c_str());
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
    rpg::login::C2SLogoutReq protoReq;
    if (!parseProto(data, len, protoReq))
        return;

    auto user = m_userManager.findUser(connID);
    if (!user)
        return;

    const auto action = protoReq.action();
    rpg::login::S2CLogoutRsp rsp;
    rsp.set_action(action);

    if (action != rpg::login::RETURN_CHAR_SELECT &&
        action != rpg::login::RETURN_LOGIN)
    {
        rsp.set_code(-1);
        rsp.set_msg("无效的退出选项");
        sendClientProtoModule(m_clientServer, connID, kLoginModule,
                     static_cast<uint8_t>(rpg::login::S2C_LOGOUT_RSP), rsp);
        logLoginFlow(LoginFlowPhase::LOGOUT, user->getAccid(), user->GetID(), connID, -1,
                     "无效 action");
        return;
    }

    const uint64_t accid = user->getAccid();
    const uint32_t zoneId = user->getZoneId();
    const UserID leavingUserId = user->GetID();

    LOG_INFO("客户端退出请求: conn=%u userID=%llu action=%s",
             connID, leavingUserId,
             action == rpg::login::RETURN_CHAR_SELECT ? "回选角" : "回登录");

    leaveWorldSession(user, true,
                      action == rpg::login::RETURN_CHAR_SELECT ? "回选角前离世界"
                                                                 : "回登录前离世界");
    resetToAccountSession(user);

    rsp.set_code(0);
    rsp.set_msg(action == rpg::login::RETURN_CHAR_SELECT ? "已返回选角" : "已退出游戏");
    sendClientProtoModule(m_clientServer, connID, kLoginModule,
                 static_cast<uint8_t>(rpg::login::S2C_LOGOUT_RSP), rsp);

    logLoginFlow(LoginFlowPhase::LOGOUT, accid, leavingUserId, connID, 0,
                 action == rpg::login::RETURN_CHAR_SELECT ? "返回选角" : "返回登录");

    if (action == rpg::login::RETURN_CHAR_SELECT)
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
            rpg::login::S2CUserList list;
            list.set_code(-1);
            sendClientProtoModule(m_clientServer, clientConn, kLoginModule,
                         static_cast<uint8_t>(rpg::login::S2C_USER_LIST), list);
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
        rpg::login::S2CLoginRsp loginRsp;
        loginRsp.set_code(1);
        loginRsp.set_msg("登录票据无效或已过期");
        user->setClientState(ClientState::CONNECTED);
        user->setAccid(0);
        user->setRoleListReady(false);
        sendClientProtoModule(m_clientServer, clientConn, kLoginModule,
                     static_cast<uint8_t>(rpg::login::S2C_LOGIN_RSP), loginRsp);
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
        rpg::login::S2CLoginRsp loginRsp;
        loginRsp.set_code(-1);
        loginRsp.set_msg("存档服务不可用，请稍后重试");
        sendClientProtoModule(m_clientServer, clientConn, kLoginModule,
                     static_cast<uint8_t>(rpg::login::S2C_LOGIN_RSP), loginRsp);
        LOG_WARN("Gateway 鉴权后无法拉取角色列表: Record 未连接 conn=%u", clientConn);
        logLoginFlow(LoginFlowPhase::GATEWAY_AUTH, rsp.accid, 0, clientConn, -1,
                     "Record 未连接");
        m_clientServer.Kick(clientConn);
        m_userManager.removeUser(clientConn);
        return;
    }

    user->setAccid(rsp.accid);
    user->setClientState(ClientState::ACCOUNT_OK);

    rpg::login::S2CLoginRsp loginRsp;
    loginRsp.set_code(0);
    loginRsp.set_user_id(0);
    loginRsp.set_accid(rsp.accid);
    loginRsp.set_msg("网关鉴权成功");
    sendClientProtoModule(m_clientServer, clientConn, kLoginModule,
                 static_cast<uint8_t>(rpg::login::S2C_LOGIN_RSP), loginRsp);

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
        rpg::login::S2CUserList list;
        list.set_code(hdr->code);
        sendClientProtoModule(m_clientServer, clientConn, kLoginModule,
                     static_cast<uint8_t>(rpg::login::S2C_USER_LIST), list);
        user->setRoleListReady(false);
        LOG_WARN("角色列表加载失败: conn=%u code=%d", clientConn, hdr->code);
        logLoginFlow(LoginFlowPhase::CHAR_LIST, user->getAccid(), 0, clientConn,
                     hdr->code, "角色列表加载失败");
        return;
    }

    constexpr size_t ENTRY_SIZE = sizeof(Msg_REC_CharacterEntryWire);
    const size_t entryBytes = static_cast<size_t>(hdr->count) * ENTRY_SIZE;
    const size_t expected = sizeof(Msg_REC_ListCharactersRspHeader) + entryBytes;
    if (len < expected)
    {
        LOG_WARN("角色列表回包体长度不足: conn=%u len=%u expected=%zu count=%u",
                 clientConn, len, expected, hdr->count);
        return;
    }

    if (hdr->count > 0 && entryBytes > CLIENT_PROTO_MAX_BODY)
    {
        LOG_WARN("角色列表回包过大，拒绝下发: conn=%u count=%u",
                 clientConn, hdr->count);
        user->setRoleListReady(false);
        return;
    }

    const auto* entries = reinterpret_cast<const Msg_REC_CharacterEntryWire*>(
        data + sizeof(Msg_REC_ListCharactersRspHeader));
    const rpg::login::S2CUserList list =
        buildUserListProto(0, entries, hdr->count);
    if (!sendClientProtoModule(m_clientServer, clientConn, kLoginModule,
                      static_cast<uint8_t>(rpg::login::S2C_USER_LIST), list))
    {
        LOG_WARN("角色列表下发失败: conn=%u count=%u", clientConn, hdr->count);
        user->setRoleListReady(false);
        return;
    }

    std::unordered_set<uint64_t> roleIds;
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

    rpg::login::S2CCreateUserRsp createRsp;
    createRsp.set_code(rsp.code);
    createRsp.set_user_id(rsp.userID);
    switch (rsp.code)
    {
    case static_cast<int32_t>(CreateCharacterError::OK):
        createRsp.set_msg("创角成功");
        break;
    case static_cast<int32_t>(CreateCharacterError::NAME_EXISTS):
        createRsp.set_msg("角色名已存在");
        break;
    case static_cast<int32_t>(CreateCharacterError::LIMIT_REACHED):
        createRsp.set_msg("角色数量已达上限");
        break;
    case static_cast<int32_t>(CreateCharacterError::INVALID_NAME):
        createRsp.set_msg("角色名非法");
        break;
    case static_cast<int32_t>(CreateCharacterError::INVALID_VOCATION):
        createRsp.set_msg("职业或性别非法");
        break;
    default:
        createRsp.set_msg("创角失败");
        break;
    }
    sendClientProtoModule(m_clientServer, clientConn, kLoginModule,
                 static_cast<uint8_t>(rpg::login::S2C_CREATE_USER_RSP), createRsp);

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
                 rsp.code, createRsp.msg().c_str());
}

void GatewayServer::onClientHeartbeat(ConnID connID, const char* data, uint16_t len)
{
    rpg::system::C2SHeartbeat req;
    if (len > 0)
        parseProto(data, len, req);

    rpg::system::S2CHeartbeat rsp;
    rsp.set_seq(req.seq());
    rsp.set_server_time(TimerMgr::NowMs());
    sendClientProtoModule(m_clientServer, connID, kSystemModule,
                  static_cast<uint8_t>(rpg::system::S2C_HEARTBEAT), rsp);
}

void GatewayServer::onUserLoginRsp(ConnID /*fromConn*/, const Msg_GW_UserLoginRsp& rsp)
{
    ConnID clientConn = rsp.gatewayClientConnID;
    auto user = m_userManager.findUser(clientConn);
    if (!user) return;

    rpg::login::S2CLoginRsp loginRsp;
    loginRsp.set_code(rsp.code);
    loginRsp.set_user_id(rsp.userID);
    if (rsp.code == 0)
    {
        user->setClientState(ClientState::IN_WORLD);
        user->setSceneServerId(rsp.sceneServerId);
        loginRsp.set_msg("进入游戏成功");

        rpg::login::S2CEnterGame enter;
        enter.set_user_id(rsp.userID);
        enter.set_map_id(rsp.mapID);
        enter.mutable_pos()->set_x(rsp.x);
        enter.mutable_pos()->set_y(rsp.y);
        enter.mutable_pos()->set_z(rsp.z);
        enter.set_level(rsp.level);
        enter.set_hp(rsp.hp);
        enter.set_max_hp(rsp.maxHP);
        enter.set_mp(rsp.mp);
        enter.set_max_mp(rsp.maxMP);
        enter.set_name(rsp.name);

        sendClientProtoModule(m_clientServer, clientConn, kLoginModule,
                     static_cast<uint8_t>(rpg::login::S2C_LOGIN_RSP), loginRsp);
        sendClientProtoModule(m_clientServer, clientConn, kLoginModule,
                     static_cast<uint8_t>(rpg::login::S2C_ENTER_GAME), enter);
        LOG_INFO("进入游戏成功: connID=%u userID=%llu map=%u sceneServerId=%u",
                 clientConn, rsp.userID, rsp.mapID, rsp.sceneServerId);
    }
    else
    {
        user->setClientState(ClientState::ACCOUNT_OK);
        loginRsp.set_msg("进入游戏失败");
        sendClientProtoModule(m_clientServer, clientConn, kLoginModule,
                     static_cast<uint8_t>(rpg::login::S2C_LOGIN_RSP), loginRsp);
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
    m_upstreamReady = false;
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
