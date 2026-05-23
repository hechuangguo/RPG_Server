#pragma once
#include "../sdk/net/TcpServer.h"
#include "../sdk/net/TcpClient.h"
#include "../sdk/util/RoleBase.h"
#include "../sdk/util/MsgDispatcher.h"
#include "../sdk/log/Logger.h"
#include "../sdk/timer/TimerMgr.h"
#include "../common/ClientMsg.h"
#include "../protocal/InternalMsg.h"
#include <unordered_map>
#include <string>

// ============================================================
//  GatewayServer —— 处理客户端连接，转发消息，登录流程
//  依赖 SceneServer、RecordServer
// ============================================================

// 客户端会话状态
enum class ClientState : uint8_t
{
    CONNECTED  = 0,   // 已连接，未登录
    LOGGING    = 1,   // 登录验证中
    LOGGED_IN  = 2,   // 已登录（已选角色）
};

struct ClientSession
{
    ConnID      connID;
    ClientState state = ClientState::CONNECTED;
    RoleID      roleID = INVALID_ROLE_ID;
    uint64_t    lastHeartbeat = 0;
};

class GatewayServer : public INetCallback
{
public:
    GatewayServer()
        : m_clientServer(this)   // 监听客户端连接
        , m_innerServer(this)    // 监听内部连接（来自 SceneServer 等）
        , m_superClient(this)
        , m_recordClient(this)
        , m_sceneClient(this)
    {}

    bool Init(uint16_t clientPort, uint16_t innerPort,
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

        RegisterHandlers();
        TimerMgr::Instance().Register(500,   0,     [this]{ RegisterToSuper(); });
        TimerMgr::Instance().Register(10000, 10000, [this]{ SendHeartbeat(); });
        TimerMgr::Instance().Register(30000, 30000, [this]{ CheckTimeout(); });
        LOG_INFO("GatewayServer started.");
        return true;
    }

    void Run()
    {
        while (true)
        {
            m_superClient.Poll(0);
            m_recordClient.Poll(0);
            m_sceneClient.Poll(0);
            // 两个服务端轮询
            m_clientServer.Poll(5);
            m_innerServer.Poll(5);
            TimerMgr::Instance().Update();
        }
    }

    // INetCallback（clientServer 和 innerServer 共用，通过 connID 区分）
    void OnConnect(ConnID id) override
    {
        // 判断是客户端还是内部连接（简化：认为小号是客户端）
        if (id < 100000)
        {
            ClientSession s;
            s.connID = id;
            s.lastHeartbeat = TimerMgr::NowMs();
            m_clients[id] = s;
            LOG_INFO("Client connected: connID=%u", id);
        }
        else
        {
            LOG_INFO("InnerServer connected: connID=%u", id);
        }
    }

    void OnDisconnect(ConnID id) override
    {
        auto it = m_clients.find(id);
        if (it != m_clients.end())
        {
            if (it->second.roleID != INVALID_ROLE_ID)
            {
                // 通知 SceneServer 角色下线
                m_sceneClient.SendMsg((uint16_t)InternalMsgID::SCE_ROLE_LEAVE,
                    reinterpret_cast<const char*>(&it->second.roleID), sizeof(RoleID));
            }
            m_clients.erase(it);
        }
    }

    void OnMessage(ConnID id, uint16_t msgID, const char* data, uint16_t len) override
    {
        // 判断是否是客户端连接
        auto cit = m_clients.find(id);
        if (cit != m_clients.end())
        {
            HandleClientMsg(id, msgID, data, len);
        }
        else
        {
            // 内部消息（来自 SceneServer 等）
            MsgDispatcher::Instance().Dispatch(id, msgID, data, len);
        }
    }

private:
    void RegisterHandlers()
    {
        auto& d = MsgDispatcher::Instance();
        // 内部消息处理
        d.Register((uint16_t)InternalMsgID::GW_SEND_TO_CLIENT,
            [this](uint32_t c, const char* d, uint16_t l){ OnSendToClient(c, d, l); });
        d.Register((uint16_t)InternalMsgID::GW_KICK_CLIENT,
            [this](uint32_t c, const char* d, uint16_t l){ OnKickClient(c, d, l); });
        d.Register((uint16_t)InternalMsgID::REC_LOGIN_VERIFY_RSP,
            [this](uint32_t c, const char* d, uint16_t l){ OnLoginVerifyRsp(c, d, l); });
    }

    void HandleClientMsg(ConnID connID, uint16_t msgID, const char* data, uint16_t len)
    {
        auto& s = m_clients[connID];
        s.lastHeartbeat = TimerMgr::NowMs();

        using CID = ClientMsgID;
        switch ((CID)msgID)
        {
        case CID::C2S_LOGIN_REQ:
            if (s.state == ClientState::CONNECTED)
                OnClientLogin(connID, data, len);
            break;
        case CID::C2S_HEARTBEAT:
            OnClientHeartbeat(connID, data, len);
            break;
        default:
            if (s.state == ClientState::LOGGED_IN)
                ForwardToScene(connID, msgID, data, len);
            break;
        }
    }

    void OnClientLogin(ConnID connID, const char* data, uint16_t len)
    {
        if (len < sizeof(Msg_C2S_LoginReq)) return;
        const auto* req = reinterpret_cast<const Msg_C2S_LoginReq*>(data);
        m_clients[connID].state = ClientState::LOGGING;

        // 转发到 RecordServer 验证
        Msg_REC_LoginVerifyReq verifyReq{};
        strncpy(verifyReq.account,  req->account,  sizeof(verifyReq.account));
        strncpy(verifyReq.password, req->password, sizeof(verifyReq.password));
        verifyReq.gatewayConnID = connID;
        m_recordClient.SendMsg((uint16_t)InternalMsgID::REC_LOGIN_VERIFY_REQ,
                                reinterpret_cast<char*>(&verifyReq), sizeof(verifyReq));
        LOG_INFO("ClientLogin: account=%s connID=%u", req->account, connID);
    }

    void OnClientHeartbeat(ConnID connID, const char* data, uint16_t len)
    {
        Msg_S2C_Heartbeat rsp{};
        if (len >= sizeof(Msg_C2S_Heartbeat))
            rsp.seq = reinterpret_cast<const Msg_C2S_Heartbeat*>(data)->seq;
        rsp.serverTime = TimerMgr::NowMs();
        m_clientServer.SendMsg(connID, (uint16_t)ClientMsgID::S2C_HEARTBEAT,
                               reinterpret_cast<char*>(&rsp), sizeof(rsp));
    }

    void OnLoginVerifyRsp(ConnID fromConn, const char* data, uint16_t len)
    {
        if (len < sizeof(Msg_REC_LoginVerifyRsp)) return;
        const auto* rsp = reinterpret_cast<const Msg_REC_LoginVerifyRsp*>(data);
        ConnID clientConn = rsp->gatewayConnID;
        auto it = m_clients.find(clientConn);
        if (it == m_clients.end()) return;

        Msg_S2C_LoginRsp loginRsp{};
        if (rsp->code == 0)
        {
            it->second.state  = ClientState::LOGGED_IN;
            it->second.roleID = rsp->roleID;
            loginRsp.code     = 0;
            loginRsp.roleID   = rsp->roleID;
            strncpy(loginRsp.msg, "Login OK", sizeof(loginRsp.msg));
            LOG_INFO("LoginSuccess: connID=%u roleID=%llu", clientConn, rsp->roleID);
        }
        else
        {
            loginRsp.code = rsp->code;
            strncpy(loginRsp.msg, "Account or password error", sizeof(loginRsp.msg));
            LOG_WARN("LoginFail: connID=%u code=%d", clientConn, rsp->code);
        }
        m_clientServer.SendMsg(clientConn, (uint16_t)ClientMsgID::S2C_LOGIN_RSP,
                               reinterpret_cast<char*>(&loginRsp), sizeof(loginRsp));
    }

    // Scene → Gateway → Client 的下行路径
    void OnSendToClient(ConnID fromConn, const char* data, uint16_t len)
    {
        if (len < sizeof(uint32_t) + sizeof(uint16_t)) return;
        uint32_t clientConnID;
        uint16_t msgID;
        memcpy(&clientConnID, data, sizeof(uint32_t));
        memcpy(&msgID,  data + sizeof(uint32_t), sizeof(uint16_t));
        const char* body    = data + sizeof(uint32_t) + sizeof(uint16_t);
        uint16_t    bodyLen = len  - sizeof(uint32_t) - sizeof(uint16_t);
        m_clientServer.SendMsg(clientConnID, msgID, body, bodyLen);
    }

    void OnKickClient(ConnID fromConn, const char* data, uint16_t len)
    {
        if (len < sizeof(uint32_t)) return;
        uint32_t clientConnID = *reinterpret_cast<const uint32_t*>(data);
        LOG_INFO("KickClient: connID=%u", clientConnID);
        m_clientServer.Kick(clientConnID);
        m_clients.erase(clientConnID);
    }

    void ForwardToScene(ConnID connID, uint16_t msgID, const char* data, uint16_t len)
    {
        // 打包成内部消息转发给 SceneServer
        std::vector<char> buf(sizeof(Msg_GW_ClientMsg) + len);
        auto* hdr = reinterpret_cast<Msg_GW_ClientMsg*>(buf.data());
        hdr->clientConnID = connID;
        hdr->msgID        = msgID;
        hdr->dataLen      = len;
        if (len > 0) memcpy(buf.data() + sizeof(Msg_GW_ClientMsg), data, len);
        m_sceneClient.SendMsg((uint16_t)InternalMsgID::GW_CLIENT_MSG,
                               buf.data(), (uint16_t)buf.size());
    }

    void CheckTimeout()
    {
        uint64_t now = TimerMgr::NowMs();
        std::vector<ConnID> expired;
        for (auto& [cid, s] : m_clients)
            if (now - s.lastHeartbeat > 60000) expired.push_back(cid);
        for (auto cid : expired)
        {
            LOG_WARN("ClientTimeout: connID=%u", cid);
            m_clientServer.Kick(cid);
            m_clients.erase(cid);
        }
    }

    void RegisterToSuper()
    {
        Msg_S2S_Register reg{};
        reg.serverType = (uint8_t)SubServerType::GATEWAY;
        reg.serverID   = 1;
        strncpy(reg.ip, "127.0.0.1", sizeof(reg.ip));
        reg.port       = 9005;
        m_superClient.SendMsg((uint16_t)InternalMsgID::S2S_REGISTER_REQ,
                               reinterpret_cast<char*>(&reg), sizeof(reg));
    }

    void SendHeartbeat()
    {
        Msg_S2S_Heartbeat hb{}; hb.seq = ++m_hbSeq; hb.timestamp = TimerMgr::NowMs();
        m_superClient.SendMsg((uint16_t)InternalMsgID::S2S_HEARTBEAT,
                               reinterpret_cast<char*>(&hb), sizeof(hb));
    }

    TcpServer m_clientServer;   // 面向客户端
    TcpServer m_innerServer;    // 面向内部服务器
    TcpClient m_superClient;
    TcpClient m_recordClient;
    TcpClient m_sceneClient;
    uint32_t  m_hbSeq = 0;
    std::unordered_map<ConnID, ClientSession> m_clients;
};
