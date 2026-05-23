/**
 * @file    GatewayServer.h
 * @brief  网关服务器 —— 客户端 TCP 接入点、登录流程控制、消息转发
 *
 * ## 职责
 * - 面向客户端：监听公网端口，接受玩家 TCP 连接
 * - 面向内部：监听内网端口，接收来自 SceneServer 的下行消息
 * - 登录流程：验证账号 → 转发给 SuperServer 调度 → 返回登录结果
 * - 消息转发：客户端 → SceneServer（上行）、SceneServer → 客户端（下行）
 * - 心跳超时检测（60 秒无心跳自动踢除）
 *
 * ## 双端口设计
 * - clientPort（外网）：客户端连接
 * - innerPort（内网）：SceneServer 等内部服务器连接
 *
 * ## 依赖关系
 * - 依赖 SuperServer + RecordServer + SceneServer
 */

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

/**
 * @brief 客户端连接状态
 */
enum class ClientState : uint8_t
{
    CONNECTED  = 0,  /**< TCP 已建立，尚未登录验证 */
    LOGGING    = 1,  /**< 登录验证中（等待 RecordServer 响应） */
    LOGGED_IN  = 2,  /**< 已登录，可以收发游戏消息 */
};

/**
 * @brief GatewayServer 维护的客户端会话信息
 */
struct ClientSession
{
    ConnID      connID;          /**< clientServer 分配的网络连接 ID */
    ClientState state = ClientState::CONNECTED;  /**< 当前状态 */
    RoleID      roleID = INVALID_ROLE_ID;        /**< 登录成功后的角色 ID */
    uint64_t    lastHeartbeat = 0;               /**< 最后一次心跳时间 */
};

/**
 * @brief GatewayServer 核心类
 *
 * 双 TcpServer：m_clientServer（客户端） + m_innerServer（内部）。
 * 通过 INetCallback::OnConnect 的 connID 区分客户端/内部连接。
 */
class GatewayServer : public INetCallback
{
public:
    GatewayServer()
        : m_clientServer(this)
        , m_innerServer(this)
        , m_superClient(this)
        , m_recordClient(this)
        , m_sceneClient(this)
    {}

    /**
     * @brief 初始化 GatewayServer
     * @param clientPort 面向客户端的监听端口
     * @param innerPort  面向内部服务器的监听端口
     * @param cfg        全局配置
     * @return 成功返回 true
     */
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

    /** @brief 主循环 */
    void Run()
    {
        while (true)
        {
            m_superClient.Poll(0);
            m_recordClient.Poll(0);
            m_sceneClient.Poll(0);
            m_clientServer.Poll(5);
            m_innerServer.Poll(5);
            TimerMgr::Instance().Update();
        }
    }

    /**
     * @brief 连接建立回调
     *
     * 通过 connID 大小区分客户端和内部连接。
     */
    void OnConnect(ConnID id) override
    {
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

    /**
     * @brief 连接断开回调
     *
     * 客户端断开时通知 SceneServer 角色下线。
     */
    void OnDisconnect(ConnID id) override
    {
        auto it = m_clients.find(id);
        if (it != m_clients.end())
        {
            if (it->second.roleID != INVALID_ROLE_ID)
            {
                m_sceneClient.SendMsg((uint16_t)InternalMsgID::SCE_ROLE_LEAVE,
                    reinterpret_cast<const char*>(&it->second.roleID), sizeof(RoleID));
            }
            m_clients.erase(it);
        }
    }

    /**
     * @brief 消息到达回调
     *
     * 区分客户端消息和内部消息分别处理。
     */
    void OnMessage(ConnID id, uint16_t msgID, const char* data, uint16_t len) override
    {
        auto cit = m_clients.find(id);
        if (cit != m_clients.end())
        {
            HandleClientMsg(id, msgID, data, len);
        }
        else
        {
            MsgDispatcher::Instance().Dispatch(id, msgID, data, len);
        }
    }

private:
    /** @brief 注册内部消息处理函数 */
    void RegisterHandlers()
    {
        auto& d = MsgDispatcher::Instance();
        d.Register((uint16_t)InternalMsgID::GW_SEND_TO_CLIENT,
            [this](uint32_t c, const char* d, uint16_t l){ OnSendToClient(c, d, l); });
        d.Register((uint16_t)InternalMsgID::GW_KICK_CLIENT,
            [this](uint32_t c, const char* d, uint16_t l){ OnKickClient(c, d, l); });
        d.Register((uint16_t)InternalMsgID::REC_LOGIN_VERIFY_RSP,
            [this](uint32_t c, const char* d, uint16_t l){ OnLoginVerifyRsp(c, d, l); });
    }

    /**
     * @brief 客户端消息处理
     *
     * @code
     *   LOGIN_REQ  → OnClientLogin（登录验证流程）
     *   HEARTBEAT  → OnClientHeartbeat（回包服务器时间）
     *   其他消息   → ForwardToScene（转发给 SceneServer）
     * @endcode
     */
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

    /**
     * @brief 处理客户端登录请求
     *
     * 设置客户端状态为 LOGGING，转发账号密码到 RecordServer 验证。
     */
    void OnClientLogin(ConnID connID, const char* data, uint16_t len)
    {
        if (len < sizeof(Msg_C2S_LoginReq)) return;
        const auto* req = reinterpret_cast<const Msg_C2S_LoginReq*>(data);
        m_clients[connID].state = ClientState::LOGGING;

        Msg_REC_LoginVerifyReq verifyReq{};
        strncpy(verifyReq.account,  req->account,  sizeof(verifyReq.account));
        strncpy(verifyReq.password, req->password, sizeof(verifyReq.password));
        verifyReq.gatewayConnID = connID;
        m_recordClient.SendMsg((uint16_t)InternalMsgID::REC_LOGIN_VERIFY_REQ,
                                reinterpret_cast<char*>(&verifyReq), sizeof(verifyReq));
        LOG_INFO("ClientLogin: account=%s connID=%u", req->account, connID);
    }

    /** @brief 处理客户端心跳 —— 直接回包 S2C_HEARTBEAT */
    void OnClientHeartbeat(ConnID connID, const char* data, uint16_t len)
    {
        Msg_S2C_Heartbeat rsp{};
        if (len >= sizeof(Msg_C2S_Heartbeat))
            rsp.seq = reinterpret_cast<const Msg_C2S_Heartbeat*>(data)->seq;
        rsp.serverTime = TimerMgr::NowMs();
        m_clientServer.SendMsg(connID, (uint16_t)ClientMsgID::S2C_HEARTBEAT,
                               reinterpret_cast<char*>(&rsp), sizeof(rsp));
    }

    /**
     * @brief 处理 RecordServer 验证响应
     *
     * 验证成功：设置 clientState=LOGGED_IN，回包给客户端。
     * 验证失败：回包错误码。
     */
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

    /**
     * @brief SceneServer → Gateway → Client 下行消息转发
     *
     * 解析包格式：[clientConnID(4)][msgID(2)][data...]
     */
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

    /** @brief 踢除客户端连接 */
    void OnKickClient(ConnID fromConn, const char* data, uint16_t len)
    {
        if (len < sizeof(uint32_t)) return;
        uint32_t clientConnID = *reinterpret_cast<const uint32_t*>(data);
        LOG_INFO("KickClient: connID=%u", clientConnID);
        m_clientServer.Kick(clientConnID);
        m_clients.erase(clientConnID);
    }

    /**
     * @brief 将客户端消息打包成内部消息转发给 SceneServer
     *
     * 包格式：[Msg_GW_ClientMsg 头部] + [原始客户端消息体]
     */
    void ForwardToScene(ConnID connID, uint16_t msgID, const char* data, uint16_t len)
    {
        std::vector<char> buf(sizeof(Msg_GW_ClientMsg) + len);
        auto* hdr = reinterpret_cast<Msg_GW_ClientMsg*>(buf.data());
        hdr->clientConnID = connID;
        hdr->msgID        = msgID;
        hdr->dataLen      = len;
        if (len > 0) memcpy(buf.data() + sizeof(Msg_GW_ClientMsg), data, len);
        m_sceneClient.SendMsg((uint16_t)InternalMsgID::GW_CLIENT_MSG,
                               buf.data(), (uint16_t)buf.size());
    }

    /**
     * @brief 心跳超时检测
     *
     * 每 30 秒检查一次，超过 60 秒无心跳的客户端踢除。
     */
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

    TcpServer m_clientServer;   /**< 面向客户端的 TCP Server */
    TcpServer m_innerServer;    /**< 面向内部服务器的 TCP Server */
    TcpClient m_superClient;    /**< 到 SuperServer 的连接 */
    TcpClient m_recordClient;   /**< 到 RecordServer 的连接 */
    TcpClient m_sceneClient;    /**< 到 SceneServer 的连接 */
    uint32_t  m_hbSeq = 0;      /**< 心跳序列号 */

    /** @brief 客户端会话表：connID → ClientSession */
    std::unordered_map<ConnID, ClientSession> m_clients;
};
