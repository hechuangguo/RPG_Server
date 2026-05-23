#pragma once
#include "../sdk/net/TcpServer.h"
#include "../sdk/util/MsgDispatcher.h"
#include "../sdk/util/RoleBase.h"
#include "../sdk/log/Logger.h"
#include "../sdk/timer/TimerMgr.h"
#include "../protocal/InternalMsg.h"
#include <unordered_map>
#include <string>

// ============================================================
//  SuperServer —— 统一管理所有服务器连接，管理角色登录逻辑
//  单进程、单线程，所有服务器优先连接到此
// ============================================================

// 子服务器类型
enum class SubServerType : uint8_t
{
    UNKNOWN       = 0,
    SESSION       = 1,
    RECORD        = 2,
    AOI           = 3,
    SCENE         = 4,
    GATEWAY       = 5,
    LOGGER        = 6,
    GLOBAL        = 7,
    ZONE          = 8,
};

struct SubServerInfo
{
    ConnID         connID;
    SubServerType  type;
    uint32_t       serverID;
    std::string    ip;
    uint16_t       port;
    bool           alive;
    uint64_t       lastHeartbeat;
};

// SuperServer 角色代理（记录角色当前在哪个 Gateway 和 Scene）
struct RoleProxy
{
    RoleID   roleID;
    ConnID   gatewayConnID;  // 对应的 GatewayServer 连接
    ConnID   sceneConnID;    // 对应的 SceneServer 连接
    uint32_t gatewayClientConnID; // 角色在 GatewayServer 里的客户端连接 ID
};

class SuperServer : public INetCallback
{
public:
    SuperServer() : m_server(this) {}

    bool Init(const std::string& ip, uint16_t port)
    {
        Logger::Instance().SetServerName("SuperServer");
        LOG_INFO("SuperServer starting on %s:%d", ip.c_str(), port);
        if (!m_server.Start(ip, port)) { LOG_FATAL("Start failed"); return false; }

        RegisterHandlers();

        // 心跳检查定时器（每 30 秒检查一次）
        TimerMgr::Instance().Register(30000, 30000, [this]{ CheckHeartbeat(); });
        LOG_INFO("SuperServer started.");
        return true;
    }

    void Run()
    {
        while (true)
        {
            m_server.Poll(10);
            TimerMgr::Instance().Update();
        }
    }

    // INetCallback
    void OnConnect(ConnID id) override
    {
        LOG_INFO("SubServer connected, connID=%u", id);
    }

    void OnDisconnect(ConnID id) override
    {
        LOG_WARN("SubServer disconnected, connID=%u", id);
        RemoveSubServer(id);
    }

    void OnMessage(ConnID id, uint16_t msgID, const char* data, uint16_t len) override
    {
        MsgDispatcher::Instance().Dispatch(id, msgID, data, len);
    }

private:
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

        // 回包
        char rsp[4] = {0};
        m_server.SendMsg(connID, (uint16_t)InternalMsgID::S2S_REGISTER_RSP, rsp, sizeof(rsp));
    }

    void OnHeartbeat(ConnID connID, const char* data, uint16_t len)
    {
        auto it = m_servers.find(connID);
        if (it != m_servers.end())
            it->second.lastHeartbeat = TimerMgr::NowMs();
        // 回包
        Msg_S2S_Heartbeat ack{};
        if (len >= sizeof(Msg_S2S_Heartbeat))
            ack.seq = reinterpret_cast<const Msg_S2S_Heartbeat*>(data)->seq;
        ack.timestamp = TimerMgr::NowMs();
        m_server.SendMsg(connID, (uint16_t)InternalMsgID::S2S_HEARTBEAT_ACK,
                         reinterpret_cast<char*>(&ack), sizeof(ack));
    }

    void OnRoleLoginReq(ConnID connID, const char* data, uint16_t len)
    {
        // GatewayServer 发来角色登录请求，分配进入 SceneServer
        if (len < sizeof(Msg_REC_LoginVerifyRsp)) return;
        const auto* rsp = reinterpret_cast<const Msg_REC_LoginVerifyRsp*>(data);
        if (rsp->code != 0) return;

        RoleProxy proxy;
        proxy.roleID              = rsp->roleID;
        proxy.gatewayConnID       = connID;
        proxy.gatewayClientConnID = rsp->gatewayConnID;
        proxy.sceneConnID         = FindSceneServer();
        m_roles[rsp->roleID]      = proxy;

        LOG_INFO("RoleLogin: roleID=%llu gatewayConn=%u sceneConn=%u",
                 rsp->roleID, connID, proxy.sceneConnID);

        // 通知 RecordServer 加载角色数据
        ConnID recConn = FindSubServer(SubServerType::RECORD);
        if (recConn != INVALID_CONN_ID)
        {
            Msg_REC_LoadRoleRsp loadReq{};
            loadReq.roleID = rsp->roleID;
            m_server.SendMsg(recConn, (uint16_t)InternalMsgID::REC_LOAD_ROLE_REQ,
                             reinterpret_cast<char*>(&loadReq), sizeof(loadReq));
        }
    }

    void OnKickRole(ConnID connID, const char* data, uint16_t len)
    {
        if (len < sizeof(RoleID)) return;
        RoleID rid = *reinterpret_cast<const RoleID*>(data);
        auto it = m_roles.find(rid);
        if (it == m_roles.end()) return;
        // 通知对应 Gateway 踢人
        m_server.SendMsg(it->second.gatewayConnID,
                         (uint16_t)InternalMsgID::GW_KICK_CLIENT,
                         reinterpret_cast<char*>(&it->second.gatewayClientConnID),
                         sizeof(uint32_t));
        m_roles.erase(it);
    }

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

    ConnID FindSubServer(SubServerType type)
    {
        for (auto& [cid, info] : m_servers)
            if (info.type == type && info.alive) return cid;
        return INVALID_CONN_ID;
    }

    ConnID FindSceneServer()
    {
        // 简单策略：取第一个存活的 SceneServer（可改为负载均衡）
        return FindSubServer(SubServerType::SCENE);
    }

    void RemoveSubServer(ConnID connID)
    {
        m_servers.erase(connID);
    }

    TcpServer m_server;
    std::unordered_map<ConnID, SubServerInfo> m_servers;
    std::unordered_map<RoleID, RoleProxy>     m_roles;
};
