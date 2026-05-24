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

/** @brief 登录流程中的待完成上下文 */
struct PendingLogin
{
    RoleID       roleID;
    ConnID       gatewayConnID;
    uint32_t     gatewayClientConnID;
    ConnID       sceneConnID;
    RoleBaseWire roleData{};
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
        d.Register((uint16_t)InternalMsgID::REC_LOAD_ROLE_RSP,
            [this](uint32_t c, const char* d, uint16_t l){ OnLoadRoleRsp(c, d, l); });
        d.Register((uint16_t)InternalMsgID::SCE_ROLE_ENTER_RSP,
            [this](uint32_t c, const char* d, uint16_t l){ OnRoleEnterRsp(c, d, l); });
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

        ConnID sceneConn = FindSceneServer();
        if (sceneConn == INVALID_CONN_ID)
        {
            LOG_WARN("RoleLogin: no SceneServer available roleID=%llu", rsp->roleID);
            SendLoginFailToGateway(connID, rsp->gatewayConnID, -1);
            return;
        }

        PendingLogin pending{};
        pending.roleID              = rsp->roleID;
        pending.gatewayConnID       = connID;
        pending.gatewayClientConnID = rsp->gatewayConnID;
        pending.sceneConnID         = sceneConn;
        m_pendingLogins[rsp->roleID] = pending;

        LOG_INFO("RoleLogin: roleID=%llu gatewayConn=%u sceneConn=%u",
                 rsp->roleID, connID, sceneConn);

        ConnID recConn = FindSubServer(SubServerType::RECORD);
        if (recConn != INVALID_CONN_ID)
        {
            RoleID rid = rsp->roleID;
            m_server.SendMsg(recConn, (uint16_t)InternalMsgID::REC_LOAD_ROLE_REQ,
                             reinterpret_cast<char*>(&rid), sizeof(rid));
        }
        else
        {
            SendLoginFailToGateway(connID, rsp->gatewayConnID, -1);
            m_pendingLogins.erase(rsp->roleID);
        }
    }

    void OnLoadRoleRsp(ConnID /*connID*/, const char* data, uint16_t len)
    {
        if (len < sizeof(Msg_REC_LoadRoleRsp)) return;
        const auto* hdr = reinterpret_cast<const Msg_REC_LoadRoleRsp*>(data);
        auto pit = m_pendingLogins.find(hdr->roleID);
        if (pit == m_pendingLogins.end()) return;

        if (hdr->code != 0 || len < sizeof(Msg_REC_LoadRoleRsp) + sizeof(RoleBaseWire))
        {
            SendLoginFailToGateway(pit->second.gatewayConnID,
                                   pit->second.gatewayClientConnID, hdr->code);
            m_pendingLogins.erase(pit);
            return;
        }

        const auto* wire = reinterpret_cast<const RoleBaseWire*>(
            data + sizeof(Msg_REC_LoadRoleRsp));

        Msg_SCE_RoleEnterReq enter{};
        enter.roleID              = wire->roleID;
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

        pit->second.roleData = *wire;

        RoleProxy proxy;
        proxy.roleID              = wire->roleID;
        proxy.gatewayConnID       = pit->second.gatewayConnID;
        proxy.gatewayClientConnID = pit->second.gatewayClientConnID;
        proxy.sceneConnID         = pit->second.sceneConnID;
        m_roles[wire->roleID]     = proxy;

        m_server.SendMsg(pit->second.sceneConnID,
                         (uint16_t)InternalMsgID::SCE_ROLE_ENTER_REQ,
                         reinterpret_cast<char*>(&enter), sizeof(enter));
    }

    void OnRoleEnterRsp(ConnID /*connID*/, const char* data, uint16_t len)
    {
        if (len < sizeof(Msg_SCE_RoleEnterRsp)) return;
        const auto* rsp = reinterpret_cast<const Msg_SCE_RoleEnterRsp*>(data);
        auto pit = m_pendingLogins.find(rsp->roleID);
        if (pit == m_pendingLogins.end()) return;

        Msg_GW_RoleLoginRsp gwRsp{};
        gwRsp.code                = rsp->code;
        gwRsp.gatewayClientConnID = rsp->gatewayClientConnID;
        gwRsp.roleID              = rsp->roleID;
        gwRsp.mapID               = rsp->mapID;
        if (rsp->code == 0)
        {
            const auto& w = pit->second.roleData;
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

        auto rit = m_roles.find(rsp->roleID);
        if (rit != m_roles.end())
        {
            m_server.SendMsg(rit->second.gatewayConnID,
                             (uint16_t)InternalMsgID::GW_ROLE_LOGIN_RSP,
                             reinterpret_cast<char*>(&gwRsp), sizeof(gwRsp));
        }

        if (rsp->code != 0)
            m_roles.erase(rsp->roleID);
        m_pendingLogins.erase(pit);
    }

    void SendLoginFailToGateway(ConnID gatewayConnID, uint32_t clientConnID, int32_t code)
    {
        Msg_GW_RoleLoginRsp gwRsp{};
        gwRsp.code                = code;
        gwRsp.gatewayClientConnID = clientConnID;
        m_server.SendMsg(gatewayConnID, (uint16_t)InternalMsgID::GW_ROLE_LOGIN_RSP,
                         reinterpret_cast<char*>(&gwRsp), sizeof(gwRsp));
    }

    /**
     * @brief 处理踢人请求
     *
     * 通知对应 GatewayServer 踢除指定角色的客户端连接。
     */
    void OnKickRole(ConnID /*connID*/, const char* data, uint16_t len)
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
    /** @brief 登录中的角色：roleID → 待完成上下文 */
    std::unordered_map<RoleID, PendingLogin>  m_pendingLogins;
};
