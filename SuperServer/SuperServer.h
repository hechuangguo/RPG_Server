/**
 * @file    SuperServer.h
 * @brief  超级服务器 —— 统一管理所有子服务器连接，协调角色登录流程
 *
 * ## 职责
 * - 作为所有子服务器的注册中心（维护路由表）
 * - 管理角色登录调度（GatewayServer → RecordServer → SceneServer）
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
#include "../sdk/util/RoleBase.h"
#include "../sdk/log/Logger.h"
#include "../sdk/timer/TimerMgr.h"
#include "../protocal/InternalMsg.h"
#include <unordered_map>
#include <string>

/**
 * @brief 子服务器类型枚举
 *
 * 与项目架构中的 9 种服务器一一对应。
 */
enum class SubServerType : uint8_t
{
    UNKNOWN       = 0,  /**< 未知类型（未注册或异常） */
    SESSION       = 1,  /**< SessionServer —— 社会关系 / 离线数据 */
    RECORD        = 2,  /**< RecordServer —— DB 读写 / 角色持久化 */
    AOI           = 3,  /**< AOIServer —— 视野管理 */
    SCENE         = 4,  /**< SceneServer —— 在线数据 / 地图逻辑 */
    GATEWAY       = 5,  /**< GatewayServer —— 客户端接入 */
    LOGGER        = 6,  /**< LoggerServer —— 日志收集 */
    GLOBAL        = 7,  /**< GlobalServer —— 全区数据（可选） */
    ZONE          = 8,  /**< ZoneServer —— 跨区转发（可选） */
};

/**
 * @brief SuperServer 维护的子服务器信息
 *
 * 每个连接的子服务器在此结构中被记录，用于路由和心跳检测。
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
 * @brief SuperServer 维护的角色代理信息
 *
 * 记录角色当前所在的 GatewayServer 和 SceneServer 连接，
 * 用于踢人、跨服路由等操作。
 */
struct RoleProxy
{
    RoleID   roleID;              /**< 角色 ID */
    ConnID   gatewayConnID;       /**< 对应 GatewayServer 的内部连接 */
    ConnID   sceneConnID;         /**< 分配到的 SceneServer 连接 */
    uint32_t gatewayClientConnID; /**< 角色在 GatewayServer 里的客户端连接 ID */
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
     * GW_ROLE_LOGIN_REQ→ OnRoleLoginReq（登录调度）
     * SS_KICK_ROLE     → OnKickRole（踢人）
     */
    void RegisterHandlers()
    {
        auto& d = MsgDispatcher::Instance();
        d.Register((uint16_t)InternalMsgID::S2S_REGISTER_REQ,
            [this](uint32_t c, const char* d, uint16_t l){ OnRegister(c, d, l); });
        d.Register((uint16_t)InternalMsgID::S2S_HEARTBEAT,
            [this](uint32_t c, const char* d, uint16_t l){ OnHeartbeat(c, d, l); });
        d.Register((uint16_t)InternalMsgID::GW_ROLE_LOGIN_REQ,
            [this](uint32_t c, const char* d, uint16_t l){ OnRoleLoginReq(c, d, l); });
        d.Register((uint16_t)InternalMsgID::SS_KICK_ROLE,
            [this](uint32_t c, const char* d, uint16_t l){ OnKickRole(c, d, l); });
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
     * @brief 处理角色登录请求
     *
     * GatewayServer 验证账号密码后将结果发给 SuperServer，
     * SuperServer 负责分配 SceneServer 并通知 RecordServer 加载角色数据。
     */
    void OnRoleLoginReq(ConnID connID, const char* data, uint16_t len)
    {
        if (len < sizeof(Msg_REC_LoginVerifyRsp)) return;
        const auto* rsp = reinterpret_cast<const Msg_REC_LoginVerifyRsp*>(data);
        if (rsp->code != 0) return;

        RoleProxy proxy;
        proxy.roleID              = rsp->roleID;
        proxy.gatewayConnID       = connID;
        proxy.gatewayClientConnID = rsp->gatewayConnID;
        proxy.sceneConnID         = FindSceneServer();  /**< 选择 SceneServer */
        m_roles[rsp->roleID]      = proxy;

        LOG_INFO("RoleLogin: roleID=%llu gatewayConn=%u sceneConn=%u",
                 rsp->roleID, connID, proxy.sceneConnID);

        ConnID recConn = FindSubServer(SubServerType::RECORD);
        if (recConn != INVALID_CONN_ID)
        {
            Msg_REC_LoadRoleRsp loadReq{};
            loadReq.roleID = rsp->roleID;
            m_server.SendMsg(recConn, (uint16_t)InternalMsgID::REC_LOAD_ROLE_REQ,
                             reinterpret_cast<char*>(&loadReq), sizeof(loadReq));
        }
    }

    /**
     * @brief 处理踢人请求
     *
     * 通知对应 GatewayServer 踢除指定角色的客户端连接。
     */
    void OnKickRole(ConnID connID, const char* data, uint16_t len)
    {
        if (len < sizeof(RoleID)) return;
        RoleID rid = *reinterpret_cast<const RoleID*>(data);
        auto it = m_roles.find(rid);
        if (it == m_roles.end()) return;
        m_server.SendMsg(it->second.gatewayConnID,
                         (uint16_t)InternalMsgID::GW_KICK_CLIENT,
                         reinterpret_cast<char*>(&it->second.gatewayClientConnID),
                         sizeof(uint32_t));
        m_roles.erase(it);
    }

    /**
     * @brief 定期心跳检查
     *
     * 遍历所有子服务器，超过 90 秒未收到心跳则标记为离线。
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
     * @brief 选择 SceneServer（简单负载均衡策略）
     *
     * 当前实现：取第一个存活的 SceneServer。
     * TODO: 可根据在线人数、地图承载等做更智能的分配。
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
    /** @brief 在线角色路由表：roleID → 代理信息 */
    std::unordered_map<RoleID, RoleProxy>     m_roles;
};
