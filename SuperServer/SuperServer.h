/**
 * @file    SuperServer.h
 * @brief  超级服务器 —— 统一管理所有子服务器连接，协调用户登录流程
 *
 * ## 职责
 * - 作为所有子服务器的注册中心（维护路由表）
 * - 管理用户登录调度（GatewayServer → RecordServer → SceneServer）
 * - 定期心跳检查（90 秒超时标记离线）
 * - 处理踢人请求（通知 GatewayServer 断开客户端）
 *
 * ## 依赖关系
 * - 不依赖任何其他服务器（最先启动）
 * - 被所有其他服务器依赖（通过 TcpClient 连接注册）
 *
 * ## 架构位置
 * @code
 *                    ┌──────────────┐
 *                    │ SuperServer  │
 *                    └──────┬───────┘
 *            ┌──────────────┼──────────────┐
 *     SessionServer   RecordServer   SceneServer ...
 * @endcode
 */

#pragma once
#include "../sdk/net/TcpServer.h"
#include "../sdk/util/MsgDispatcher.h"
#include "../sdk/util/UserBase.h"
#include "../sdk/log/Logger.h"
#include "../sdk/timer/TimerMgr.h"
#include "../protocal/InternalMsg.h"
#include <unordered_map>
#include <string>

/**
 * @brief SuperServer 维护的子服务器信息
 *
 * 每个连接的子服务器在此结构中被记录，用于路由和心跳检测。
 * SubServerType 定义见 InternalMsg.h。
 */
struct SubServerInfo
{
    ConnID         connID;         /**< 与 SuperServer 的连接 ID */
    SubServerType  type;           /**< 服务器类型 */
    uint32_t       serverID;       /**< 服务器实例编号 */
    std::string    ip;             /**< 监听 IP */
    uint16_t       port;           /**< 监听端口 */
    bool           alive;          /**< 是否存活（心跳超时则置 false） */
    uint64_t       lastHeartbeat;  /**< 最后一次心跳时间戳（ms） */
};

/**
 * @brief SuperServer 维护的用户代理信息
 *
 * 记录用户当前所在的 GatewayServer 和 SceneServer 连接，
 * 用于踢人、跨服路由等操作。
 *
 * 字段关系说明：
 * - gatewayConnID 与 sceneConnID 分别指向用户关联的两个子服务器内部连接，
 *   二者通过 SuperServer 路由表关联，但彼此无直接依赖。
 * - gatewayClientConnID 是 GatewayServer 内部为客户端分配的连接标识，
 *   在踢人操作时需要通过 gatewayConnID 定位 GatewayServer，
 *   再通过 gatewayClientConnID 定位具体客户端连接。
 * - userID 作为唯一索引，将上述三个连接信息绑定为一个用户在线路由条目。
 */
struct UserProxy
{
    UserID   userID;              /**< 用户 ID */
    ConnID   gatewayConnID;       /**< 对应 GatewayServer 的内部连接 */
    ConnID   sceneConnID;         /**< 分配到的 SceneServer 连接 */
    uint32_t gatewayClientConnID; /**< 用户在 GatewayServer 里的客户端连接 ID */
};

/** @brief 登录流程中的待完成上下文 */
struct PendingLogin
{
    UserID       userID;
    ConnID       gatewayConnID;
    uint32_t     gatewayClientConnID;
    ConnID       sceneConnID;
    UserBaseWire userData{};
};

/**
 * @brief SuperServer 核心类
 *
 * 实现 INetCallback，通过 TcpServer 监听所有子服务器的 TCP 长连接。
 * 单例模式运行（一个游戏区仅一个进程）。
 */
class SuperServer : public INetCallback
{
public:
    SuperServer() : m_server(this) {}

    /**
     * @brief 初始化 SuperServer
     * @param ip   监听 IP
     * @param port 监听端口
     * @return 成功返回 true
     *
     * 注册消息处理函数，启动 30 秒间隔心跳检查定时器。
     */
    bool Init(const std::string& ip, uint16_t port)
    {
        Logger::Instance().SetServerName("SuperServer");
        LOG_INFO("SuperServer starting on %s:%d", ip.c_str(), port);
        if (!m_server.Start(ip, port)) { LOG_FATAL("Start failed"); return false; }

        RegisterHandlers();

        TimerMgr::Instance().Register(30000, 30000, [this]{ CheckHeartbeat(); });
        LOG_INFO("SuperServer started.");
        return true;
    }

    /** @brief 主循环：轮询网络事件 + 驱动定时器 */
    void Run()
    {
        while (true)
        {
            m_server.Poll(10);
            TimerMgr::Instance().Update();
        }
    }

    // ============================================================
    //  INetCallback 实现
    // ============================================================

    void OnConnect(ConnID id) override
    {
        LOG_INFO("SubServer connected, connID=%u", id);
    }

    /**
     * @brief 子服务器断开
     *
     * 移除该服务器的路由表记录，后续 FindSubServer 将返回 INVALID_CONN_ID。
     */
    void OnDisconnect(ConnID id) override
    {
        LOG_WARN("SubServer disconnected, connID=%u", id);
        RemoveSubServer(id);
    }

    /** @brief 消息到达后派发给 MsgDispatcher */
    void OnMessage(ConnID id, uint16_t msgID, const char* data, uint16_t len) override
    {
        MsgDispatcher::Instance().Dispatch(id, msgID, data, len);
    }

private:
    /**
     * @brief 注册所有消息处理函数
     *
     * S2S_REGISTER_REQ → OnRegister（注册）
     * S2S_HEARTBEAT    → OnHeartbeat（心跳）
     * GW_USER_LOGIN_REQ→ OnUserLoginReq（登录调度）
     * SS_KICK_USER     → OnKickUser（踢人）
     */
    void RegisterHandlers()
    {
        auto& d = MsgDispatcher::Instance();
        d.Register((uint16_t)InternalMsgID::S2S_REGISTER_REQ,
            [this](uint32_t c, const char* d, uint16_t l){ OnRegister(c, d, l); });
        d.Register((uint16_t)InternalMsgID::S2S_HEARTBEAT,
            [this](uint32_t c, const char* d, uint16_t l){ OnHeartbeat(c, d, l); });
        d.Register((uint16_t)InternalMsgID::GW_USER_LOGIN_REQ,
            [this](uint32_t c, const char* d, uint16_t l){ OnUserLoginReq(c, d, l); });
        d.Register((uint16_t)InternalMsgID::REC_LOAD_USER_RSP,
            [this](uint32_t c, const char* d, uint16_t l){ OnLoadUserRsp(c, d, l); });
        d.Register((uint16_t)InternalMsgID::SCE_USER_ENTER_RSP,
            [this](uint32_t c, const char* d, uint16_t l){ OnUserEnterRsp(c, d, l); });
        d.Register((uint16_t)InternalMsgID::SS_KICK_USER,
            [this](uint32_t c, const char* d, uint16_t l){ OnKickUser(c, d, l); });
    }

    /**
     * @brief 处理子服务器注册
     *
     * 收到 Msg_S2S_Register 后记录服务器信息到 m_servers，
     * 并回复 S2S_REGISTER_RSP 确认。
     */
    void OnRegister(ConnID connID, const char* data, uint16_t len)
    {
        if (len < sizeof(Msg_S2S_Register)) return;
        const auto* req = reinterpret_cast<const Msg_S2S_Register*>(data);
        SubServerInfo info;
        info.connID        = connID;
        info.type          = (SubServerType)req->serverType;
        info.serverID      = req->serverID;
        info.ip            = req->ip;
        info.port          = req->port;
        info.alive         = true;
        info.lastHeartbeat = TimerMgr::NowMs();
        m_servers[connID]  = info;
        LOG_INFO("SubServer registered: type=%d serverID=%u ip=%s port=%d",
                 (int)info.type, info.serverID, info.ip.c_str(), info.port);

        char rsp[4] = {0};
        m_server.SendMsg(connID, (uint16_t)InternalMsgID::S2S_REGISTER_RSP, rsp, sizeof(rsp));
    }

    /**
     * @brief 处理心跳
     *
     * 更新 lastHeartbeat 时间戳并回复 ACK（含服务器时间）。
     */
    void OnHeartbeat(ConnID connID, const char* data, uint16_t len)
    {
        auto it = m_servers.find(connID);
        if (it != m_servers.end())
            it->second.lastHeartbeat = TimerMgr::NowMs();

        Msg_S2S_Heartbeat ack{};
        if (len >= sizeof(Msg_S2S_Heartbeat))
            ack.seq = reinterpret_cast<const Msg_S2S_Heartbeat*>(data)->seq;
        ack.timestamp = TimerMgr::NowMs();
        m_server.SendMsg(connID, (uint16_t)InternalMsgID::S2S_HEARTBEAT_ACK,
                         reinterpret_cast<char*>(&ack), sizeof(ack));
    }

    /**
     * @brief 处理用户登录请求
     *
     * GatewayServer 验证账号密码后将结果发给 SuperServer，
     * SuperServer 负责分配 SceneServer 并通知 RecordServer 加载用户数据。
     */
    void OnUserLoginReq(ConnID connID, const char* data, uint16_t len)
    {
        if (len < sizeof(Msg_REC_LoginVerifyRsp)) return;
        const auto* rsp = reinterpret_cast<const Msg_REC_LoginVerifyRsp*>(data);
        if (rsp->code != 0) return;

        ConnID sceneConn = FindSceneServer();
        if (sceneConn == INVALID_CONN_ID)
        {
            LOG_WARN("UserLogin: no SceneServer available userID=%llu", rsp->userID);
            SendLoginFailToGateway(connID, rsp->gatewayConnID, -1);
            return;
        }

        PendingLogin pending{};
        pending.userID              = rsp->userID;
        pending.gatewayConnID       = connID;
        pending.gatewayClientConnID = rsp->gatewayConnID;
        pending.sceneConnID         = sceneConn;
        m_pendingLogins[rsp->userID] = pending;

        LOG_INFO("UserLogin: userID=%llu gatewayConn=%u sceneConn=%u",
                 rsp->userID, connID, sceneConn);

        ConnID recConn = FindSubServer(SubServerType::RECORD);
        if (recConn != INVALID_CONN_ID)
        {
            UserID uid = rsp->userID;
            m_server.SendMsg(recConn, (uint16_t)InternalMsgID::REC_LOAD_USER_REQ,
                             reinterpret_cast<char*>(&uid), sizeof(uid));
        }
        else
        {
            SendLoginFailToGateway(connID, rsp->gatewayConnID, -1);
            m_pendingLogins.erase(rsp->userID);
        }
    }

    void OnLoadUserRsp(ConnID /*connID*/, const char* data, uint16_t len)
    {
        if (len < sizeof(Msg_REC_LoadUserRsp)) return;
        const auto* hdr = reinterpret_cast<const Msg_REC_LoadUserRsp*>(data);
        auto pit = m_pendingLogins.find(hdr->userID);
        if (pit == m_pendingLogins.end()) return;

        if (hdr->code != 0 || len < sizeof(Msg_REC_LoadUserRsp) + sizeof(UserBaseWire))
        {
            SendLoginFailToGateway(pit->second.gatewayConnID,
                                   pit->second.gatewayClientConnID, hdr->code);
            m_pendingLogins.erase(pit);
            return;
        }

        const auto* wire = reinterpret_cast<const UserBaseWire*>(
            data + sizeof(Msg_REC_LoadUserRsp));

        Msg_SCE_UserEnterReq enter{};
        enter.userID              = wire->userID;
        enter.mapID               = wire->mapID ? wire->mapID : 1001;
        enter.x                   = wire->posX;
        enter.y                   = wire->posY;
        enter.z                   = wire->posZ;
        enter.gatewayClientConnID = pit->second.gatewayClientConnID;
        snprintf(enter.name, sizeof(enter.name), "%s", wire->name);
        enter.level    = wire->level;
        enter.vocation = wire->vocation;
        enter.sex      = wire->sex;
        enter.hp       = wire->hp;
        enter.maxHP    = wire->maxHP;
        enter.mp       = wire->mp;
        enter.maxMP    = wire->maxMP;
        enter.gold     = wire->gold;

        pit->second.userData = *wire;

        UserProxy proxy;
        proxy.userID              = wire->userID;
        proxy.gatewayConnID       = pit->second.gatewayConnID;
        proxy.gatewayClientConnID = pit->second.gatewayClientConnID;
        proxy.sceneConnID         = pit->second.sceneConnID;
        m_users[wire->userID]     = proxy;

        m_server.SendMsg(pit->second.sceneConnID,
                         (uint16_t)InternalMsgID::SCE_USER_ENTER_REQ,
                         reinterpret_cast<char*>(&enter), sizeof(enter));
    }

    void OnUserEnterRsp(ConnID /*connID*/, const char* data, uint16_t len)
    {
        if (len < sizeof(Msg_SCE_UserEnterRsp)) return;
        const auto* rsp = reinterpret_cast<const Msg_SCE_UserEnterRsp*>(data);
        auto pit = m_pendingLogins.find(rsp->userID);
        if (pit == m_pendingLogins.end()) return;

        Msg_GW_UserLoginRsp gwRsp{};
        gwRsp.code                = rsp->code;
        gwRsp.gatewayClientConnID = rsp->gatewayClientConnID;
        gwRsp.userID              = rsp->userID;
        gwRsp.mapID               = rsp->mapID;
        if (rsp->code == 0)
        {
            const auto& w = pit->second.userData;
            gwRsp.x     = w.posX;
            gwRsp.y     = w.posY;
            gwRsp.z     = w.posZ;
            gwRsp.level = w.level;
            gwRsp.hp    = w.hp;
            gwRsp.maxHP = w.maxHP;
            gwRsp.mp    = w.mp;
            gwRsp.maxMP = w.maxMP;
            snprintf(gwRsp.name, sizeof(gwRsp.name), "%s", w.name);
        }

        auto rit = m_users.find(rsp->userID);
        if (rit != m_users.end())
        {
            m_server.SendMsg(rit->second.gatewayConnID,
                             (uint16_t)InternalMsgID::GW_USER_LOGIN_RSP,
                             reinterpret_cast<char*>(&gwRsp), sizeof(gwRsp));
        }

        if (rsp->code != 0)
            m_users.erase(rsp->userID);
        m_pendingLogins.erase(pit);
    }

    void SendLoginFailToGateway(ConnID gatewayConnID, uint32_t clientConnID, int32_t code)
    {
        Msg_GW_UserLoginRsp gwRsp{};
        gwRsp.code                = code;
        gwRsp.gatewayClientConnID = clientConnID;
        m_server.SendMsg(gatewayConnID, (uint16_t)InternalMsgID::GW_USER_LOGIN_RSP,
                         reinterpret_cast<char*>(&gwRsp), sizeof(gwRsp));
    }

    /**
     * @brief 处理踢人请求
     *
     * 通知对应 GatewayServer 踢除指定用户的客户端连接。
     */
    void OnKickUser(ConnID /*connID*/, const char* data, uint16_t len)
    {
        if (len < sizeof(UserID)) return;
        UserID uid = *reinterpret_cast<const UserID*>(data);
        auto it = m_users.find(uid);
        if (it == m_users.end()) return;
        m_server.SendMsg(it->second.gatewayConnID,
                         (uint16_t)InternalMsgID::GW_KICK_CLIENT,
                         reinterpret_cast<char*>(&it->second.gatewayClientConnID),
                         sizeof(uint32_t));
        m_users.erase(it);
    }

    /**
     * @brief 定期心跳检查
     *
     * 遍历所有子服务器，超过 90 秒未收到心跳则标记为离线。
     *
     * 心跳超时处理流程：
     * 1. 获取当前时间戳（毫秒）。
     * 2. 遍历 m_servers 路由表中的所有子服务器条目。
     * 3. 对每个子服务器，计算当前时间与 lastHeartbeat 的差值。
     * 4. 若差值超过 90000ms（90 秒），则将该服务器的 alive 标记为 false。
     * 5. 日志记录超时服务器的 connID 和类型，便于运维排查。
     * 6. 注意：此处仅标记离线（软断开），不主动关闭 TCP 连接。连接的实际
     *    断开由底层网络事件触发 OnDisconnect 回调处理。标记为离线的服务器
     *    将不会被 FindSubServer 选中，从而实现故障隔离。
     */
    void CheckHeartbeat()
    {
        uint64_t now = TimerMgr::NowMs();
        for (auto& [cid, info] : m_servers)
        {
            if (now - info.lastHeartbeat > 90000)
            {
                LOG_WARN("SubServer timeout: connID=%u type=%d", cid, (int)info.type);
                info.alive = false;
            }
        }
    }

    /**
     * @brief 查找指定类型的子服务器连接
     * @param type 服务器类型
     * @return 找到的连接 ID，未找到返回 INVALID_CONN_ID
     */
    ConnID FindSubServer(SubServerType type)
    {
        for (auto& [cid, info] : m_servers)
            if (info.type == type && info.alive) return cid;
        return INVALID_CONN_ID;
    }

    /**
     * @brief 选择 SceneServer（负载均衡策略）
     *
     * 负载均衡策略详细说明：
     * 当前采用"首个存活"策略（First-Alive），遍历 m_servers 路由表，
     * 返回第一个类型为 SCENE 且 alive == true 的服务器连接。
     *
     * 策略特点：
     * - 实现简单，无需额外统计信息，适合固定数量的 SceneServer 部署。
     * - 所有登录请求持续分配到同一台 SceneServer，直至该服务器离线。
     * - 当前选中服务器离线后自动切换到路由表中下一个存活的 SceneServer。
     *
     * 可扩展方向（TODO）：
     * - 轮询（Round-Robin）：在多个存活 SceneServer 间轮流分配，均匀分摊负载。
     * - 最少连接（Least-Connections）：优先选择当前在线用户数最少的 SceneServer。
     * - 哈希取模（Hash）：根据 userID 取模固定分配到指定服务器，适合有状态场景。
     * - 加权分配（Weighted）：根据各 SceneServer 的硬件配置（CPU/内存）赋予不同权重。
     *
     * @return 选中的 SceneServer 连接 ID，无可用服务器返回 INVALID_CONN_ID
     */
    ConnID FindSceneServer()
    {
        return FindSubServer(SubServerType::SCENE);
    }

    /** @brief 从路由表中删除指定连接 */
    void RemoveSubServer(ConnID connID)
    {
        m_servers.erase(connID);
    }

    TcpServer m_server;  /**< 监听所有子服务器的 TCP Server */

    /** @brief 子服务器路由表：connID → 服务器信息 */
    std::unordered_map<ConnID, SubServerInfo> m_servers;
    /** @brief 在线用户路由表：userID → 代理信息 */
    std::unordered_map<UserID, UserProxy>     m_users;
    /** @brief 登录中的用户：userID → 待完成上下文 */
    std::unordered_map<UserID, PendingLogin>  m_pendingLogins;
};
