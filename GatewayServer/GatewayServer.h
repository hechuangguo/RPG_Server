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
 * - 外网端口 (clientPort = 9005)：客户端通过公网连接，进行登录和游戏数据交互
 * - 内网端口 (innerPort = 19005)：SceneServer 等内部服务器通过内网连接，传递下行消息和踢人指令
 * - 通过 connID 大小区分客户端连接（< 100000）和内部服务器连接（>= 100000）
 *
 * ## 依赖关系
 * - 依赖 SuperServer + RecordServer + SceneServer
 */

#pragma once
#include "../sdk/net/TcpServer.h"
#include "../sdk/net/TcpClient.h"
#include "../sdk/util/UserBase.h"
#include "../sdk/util/MsgDispatcher.h"
#include "../sdk/util/ConfigLoader.h"
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
    UserID      userID = INVALID_USER_ID;        /**< 登录成功后的用户 ID */
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

        m_clientPort = clientPort;
        m_innerPort  = innerPort;

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
     * - connID < 100000：客户端连接，创建 ClientSession 记录
     * - connID >= 100000：内部服务器连接（如 SceneServer），直接记录日志
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
     * 客户端断开时通知 SceneServer 用户下线。
     */
    void OnDisconnect(ConnID id) override
    {
        auto it = m_clients.find(id);
        if (it != m_clients.end())
        {
            if (it->second.userID != INVALID_USER_ID)
            {
                m_sceneClient.SendMsg((uint16_t)InternalMsgID::SCE_USER_LEAVE,
                    reinterpret_cast<const char*>(&it->second.userID), sizeof(UserID));
            }
            m_clients.erase(it);
        }
    }

    /**
     * @brief 消息到达回调
     *
     * 区分客户端消息和内部消息分别处理。
     * - 客户端连接：通过 HandleClientMsg 处理登录、心跳和游戏消息
     * - 内部连接：通过 MsgDispatcher 分发到对应的处理函数
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
    /**
     * @brief 注册内部消息处理函数
     *
     * 注册四个内部消息的处理回调：
     * - GW_SEND_TO_CLIENT：SceneServer 发往客户端的下行消息
     * - GW_KICK_CLIENT：主动踢除客户端连接
     * - REC_LOGIN_VERIFY_RSP：RecordServer 的登录验证响应
     * - GW_USER_LOGIN_RSP：SuperServer 完成登录调度后的响应
     */
    void RegisterHandlers()
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

    /**
     * @brief 客户端消息处理
     *
     * 根据消息类型分发到对应处理函数：
     * - C2S_LOGIN_REQ  → OnClientLogin（登录验证流程，仅 CONNECTED 状态可触发）
     * - C2S_HEARTBEAT  → OnClientHeartbeat（回包服务器时间）
     * - 其他消息       → ForwardToScene（将消息转发给 SceneServer，仅 LOGGED_IN 状态可触发）
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
     * 收到客户端的 C2S_LOGIN_REQ 消息后：
     * 1. 将客户端状态设为 LOGGING，防止重复登录
     * 2. 提取账号密码，构造 Msg_REC_LoginVerifyReq
     * 3. 附上网关侧的 connID，发送给 RecordServer 进行验证
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

    /**
     * @brief 处理客户端心跳
     *
     * 直接回包客户端 S2C_HEARTBEAT，包含客户端发来的序列号和服务器当前时间，
     * 用于客户端计算 RTT 和维持连接活跃状态。
     */
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
     * 接收 REC_LOGIN_VERIFY_RSP 消息后的处理逻辑：
     * - 验证失败（rsp->code != 0）：回包客户端 S2C_LOGIN_RSP 错误信息，状态恢复为 CONNECTED
     * - 验证成功（rsp->code == 0）：记录 userID，转发 GW_USER_LOGIN_REQ 给 SuperServer 完成服务器调度
     */
    void OnLoginVerifyRsp(ConnID /*fromConn*/, const char* data, uint16_t len)
    {
        if (len < sizeof(Msg_REC_LoginVerifyRsp)) return;
        const auto* rsp = reinterpret_cast<const Msg_REC_LoginVerifyRsp*>(data);
        ConnID clientConn = rsp->gatewayConnID;
        auto it = m_clients.find(clientConn);
        if (it == m_clients.end()) return;

        if (rsp->code != 0)
        {
            Msg_S2C_LoginRsp loginRsp{};
            loginRsp.code = rsp->code;
            strncpy(loginRsp.msg, "Account or password error", sizeof(loginRsp.msg));
            it->second.state = ClientState::CONNECTED;
            m_clientServer.SendMsg(clientConn, (uint16_t)ClientMsgID::S2C_LOGIN_RSP,
                                   reinterpret_cast<char*>(&loginRsp), sizeof(loginRsp));
            LOG_WARN("LoginFail: connID=%u code=%d", clientConn, rsp->code);
            return;
        }

        it->second.userID = rsp->userID;
        m_superClient.SendMsg((uint16_t)InternalMsgID::GW_USER_LOGIN_REQ,
                              data, len);
        LOG_INFO("LoginVerifyOK, forwarding to Super: connID=%u userID=%llu",
                 clientConn, rsp->userID);
    }

    /**
     * @brief 处理 SuperServer 登录调度响应
     *
     * 接收 GW_USER_LOGIN_RSP 消息后的处理逻辑：
     * - 成功（rsp->code == 0）：客户端状态设为 LOGGED_IN，发送 S2C_LOGIN_RSP 和 S2C_ENTER_GAME
     * - 失败：状态恢复为 CONNECTED，返回错误信息给客户端
     */
    void OnUserLoginRsp(ConnID /*fromConn*/, const char* data, uint16_t len)
    {
        if (len < sizeof(Msg_GW_UserLoginRsp)) return;
        const auto* rsp = reinterpret_cast<const Msg_GW_UserLoginRsp*>(data);
        ConnID clientConn = rsp->gatewayClientConnID;
        auto it = m_clients.find(clientConn);
        if (it == m_clients.end()) return;

        Msg_S2C_LoginRsp loginRsp{};
        loginRsp.code   = rsp->code;
        loginRsp.userID = rsp->userID;
        if (rsp->code == 0)
        {
            it->second.state = ClientState::LOGGED_IN;
            strncpy(loginRsp.msg, "Login OK", sizeof(loginRsp.msg));

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
            it->second.state = ClientState::CONNECTED;
            strncpy(loginRsp.msg, "Enter game failed", sizeof(loginRsp.msg));
            m_clientServer.SendMsg(clientConn, (uint16_t)ClientMsgID::S2C_LOGIN_RSP,
                                   reinterpret_cast<char*>(&loginRsp), sizeof(loginRsp));
        }
    }

    /**
     * @brief SceneServer → Gateway → Client 下行消息转发
     *
     * 接收 GW_SEND_TO_CLIENT 消息，解析内部包格式 [clientConnID(4字节)][msgID(2字节)][data...]，
     * 提取目标客户端连接 ID 后通过 m_clientServer 发送给对应客户端。
     */
    void OnSendToClient(ConnID /*fromConn*/, const char* data, uint16_t len)
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

    /**
     * @brief 踢除指定客户端连接
     *
     * 接收 GW_KICK_CLIENT 消息，包体为 4 字节的 clientConnID。
     * 主动断开客户端 TCP 连接并从会话表中删除。
     */
    void OnKickClient(ConnID /*fromConn*/, const char* data, uint16_t len)
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
     * 构造 Msg_GW_ClientMsg 头部（包含 clientConnID、msgID、dataLen），
     * 后接原始客户端消息体，通过 m_sceneClient 发送 GW_CLIENT_MSG 给 SceneServer 进行处理。
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
     * 每 30 秒由定时器触发一次，遍历所有客户端会话：
     * - lastHeartbeat 距当前时间超过 60 秒的视为超时
     * - 超时客户端被踢除并从会话表中删除
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

    /**
     * @brief 向 SuperServer 注册
     *
     * 500ms 后由定时器触发，发送 S2S_REGISTER_REQ 消息，
     * 告知 SuperServer 本网关的服务器类型、ID、IP 和客户端端口，
     * 用于 SuperServer 统一管理所有子服务器。
     */
    void RegisterToSuper()
    {
        Msg_S2S_Register reg{};
        reg.serverType = (uint8_t)SubServerType::GATEWAY;
        reg.serverID   = 1;
        strncpy(reg.ip, "127.0.0.1", sizeof(reg.ip));
        reg.port       = m_clientPort;
        m_superClient.SendMsg((uint16_t)InternalMsgID::S2S_REGISTER_REQ,
                               reinterpret_cast<char*>(&reg), sizeof(reg));
    }

    /**
     * @brief 向 SuperServer 发送心跳
     *
     * 每 10 秒由定时器触发，发送 S2S_HEARTBEAT 消息，
     * 携带递增的序列号和服务器时间戳，维持与 SuperServer 的活跃连接。
     * SuperServer 可通过此心跳判断网关是否存活。
     */
    void SendHeartbeat()
    {
        Msg_S2S_Heartbeat hb{}; hb.seq = ++m_hbSeq; hb.timestamp = TimerMgr::NowMs();
        m_superClient.SendMsg((uint16_t)InternalMsgID::S2S_HEARTBEAT,
                               reinterpret_cast<char*>(&hb), sizeof(hb));
    }

    // ======================== 成员变量 ========================

    // --- 网络服务 ---
    TcpServer m_clientServer;   /**< 面向客户端的 TCP Server，监听外网端口 9005，接受玩家连接 */
    TcpServer m_innerServer;    /**< 面向内部服务器的 TCP Server，监听内网端口 19005，接收 SceneServer 等内部连接 */

    // --- 上游连接 ---
    TcpClient m_superClient;    /**< 到 SuperServer 的连接，用于注册和登录调度 */
    TcpClient m_recordClient;   /**< 到 RecordServer 的连接，用于账号密码验证 */
    TcpClient m_sceneClient;    /**< 到 SceneServer 的连接，用于上行消息转发和下行消息接收 */

    // --- 状态 ---
    uint32_t  m_hbSeq = 0;      /**< 心跳序列号，每次发送心跳递增 */

    // --- 双端口配置 ---
    uint16_t  m_clientPort = 9005;   /**< 外网端口：客户端通过公网连接此端口进行登录和游戏数据交互 */
    uint16_t  m_innerPort  = 19005;  /**< 内网端口：内部服务器（SceneServer 等）通过内网连接此端口发送下行消息 */

    // --- 客户端管理 ---
    /**
     * @brief 客户端会话表
     *
     * key=connID, value=ClientSession。维护所有已建立 TCP 连接的客户端状态，
     * 包括连接状态、用户 ID 和最后心跳时间，用于：
     * - 消息路由：根据 connID 找到对应的 ClientSession，完成状态判定和消息转发
     * - 超时检测：定时器检查 lastHeartbeat，超过阈值则踢除
     * - 下线通知：客户端断开时获取 userID 通知 SceneServer
     */
    std::unordered_map<ConnID, ClientSession> m_clients;
};
