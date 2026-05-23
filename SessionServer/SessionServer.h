#pragma once
#include "../sdk/net/TcpServer.h"
#include "../sdk/net/TcpClient.h"
#include "../sdk/util/MsgDispatcher.h"
#include "../sdk/util/RoleBase.h"
#include "../sdk/log/Logger.h"
#include "../sdk/timer/TimerMgr.h"
#include "../protocal/InternalMsg.h"
#include <unordered_map>
#include <vector>
#include <string>

// ============================================================
//  SessionServer —— 处理角色社会关系数据、离线数据
//  依赖 SuperServer
// ============================================================

// 社会关系数据
struct SocialData
{
    RoleID               roleID;
    std::vector<RoleID>  friends;
    std::vector<RoleID>  blackList;
    uint64_t             guildID = 0;
    uint32_t             teamID  = 0;
};

// SessionServer 维护的角色代理
class SessionRole : public IRole
{
public:
    explicit SessionRole(const RoleBase& base) : IRole(base) {}
    SocialData& Social() { return m_social; }
private:
    SocialData m_social;
};

// 离线消息
struct OfflineMsg
{
    RoleID   toID;
    uint16_t msgID;
    std::vector<char> data;
};

class SessionServer : public INetCallback
{
public:
    SessionServer() : m_server(this), m_superClient(this) {}

    bool Init(const std::string& ip, uint16_t port,
              const std::string& superIP, uint16_t superPort)
    {
        Logger::Instance().SetServerName("SessionServer");
        LOG_INFO("SessionServer starting on %s:%d", ip.c_str(), port);
        if (!m_server.Start(ip, port)) { LOG_FATAL("Start failed"); return false; }

        // 连接 SuperServer
        if (!m_superClient.Connect(superIP, superPort))
        { LOG_WARN("Cannot connect to SuperServer"); }

        RegisterHandlers();

        // 向 SuperServer 注册
        TimerMgr::Instance().Register(500, 0, [this]{ RegisterToSuper(); });
        // 心跳
        TimerMgr::Instance().Register(10000, 10000, [this]{ SendHeartbeat(); });
        LOG_INFO("SessionServer started.");
        return true;
    }

    void Run()
    {
        while (true)
        {
            m_superClient.Poll(0);
            m_server.Poll(10);
            TimerMgr::Instance().Update();
        }
    }

    // INetCallback（服务器侧监听）
    void OnConnect(ConnID id) override { LOG_INFO("InnerConn connected=%u", id); }
    void OnDisconnect(ConnID id) override { LOG_INFO("InnerConn disconnected=%u", id); }
    void OnMessage(ConnID id, uint16_t msgID, const char* data, uint16_t len) override
    {
        MsgDispatcher::Instance().Dispatch(id, msgID, data, len);
    }

private:
    void RegisterHandlers()
    {
        auto& d = MsgDispatcher::Instance();
        d.Register((uint16_t)InternalMsgID::S2S_HEARTBEAT_ACK,
            [](uint32_t, const char*, uint16_t){ /* 收到心跳 ACK */ });
        d.Register((uint16_t)InternalMsgID::SES_LOAD_ROLE_REQ,
            [this](uint32_t c, const char* d, uint16_t l){ OnLoadRoleReq(c, d, l); });
        d.Register((uint16_t)InternalMsgID::SES_SAVE_ROLE_REQ,
            [this](uint32_t c, const char* d, uint16_t l){ OnSaveRoleReq(c, d, l); });
        d.Register((uint16_t)InternalMsgID::SES_FRIEND_UPDATE,
            [this](uint32_t c, const char* d, uint16_t l){ OnFriendUpdate(c, d, l); });
    }

    void RegisterToSuper()
    {
        Msg_S2S_Register reg{};
        reg.serverType = (uint8_t)SubServerType::SESSION;
        reg.serverID   = 1;
        strncpy(reg.ip, "127.0.0.1", sizeof(reg.ip));
        reg.port       = 9001;
        m_superClient.SendMsg((uint16_t)InternalMsgID::S2S_REGISTER_REQ,
                               reinterpret_cast<char*>(&reg), sizeof(reg));
    }

    void SendHeartbeat()
    {
        Msg_S2S_Heartbeat hb{};
        hb.seq       = ++m_hbSeq;
        hb.timestamp = TimerMgr::NowMs();
        m_superClient.SendMsg((uint16_t)InternalMsgID::S2S_HEARTBEAT,
                               reinterpret_cast<char*>(&hb), sizeof(hb));
    }

    void OnLoadRoleReq(ConnID fromConn, const char* data, uint16_t len)
    {
        if (len < sizeof(RoleID)) return;
        RoleID rid = *reinterpret_cast<const RoleID*>(data);
        LOG_DEBUG("LoadRole req roleID=%llu", rid);

        auto it = m_roles.find(rid);
        if (it == m_roles.end())
        {
            // 新建
            RoleBase base; base.roleID = rid;
            m_roles.emplace(rid, std::make_shared<SessionRole>(base));
        }
        // 回包：社会关系数据（此处简化）
        Msg_SES_LoadRoleRsp rsp{};  // 需在协议中定义，此处占位
        (void)rsp;
        m_server.SendMsg(fromConn, (uint16_t)InternalMsgID::SES_LOAD_ROLE_RSP,
                         data, len); // 回显 roleID
    }

    void OnSaveRoleReq(ConnID fromConn, const char* data, uint16_t len)
    {
        if (len < sizeof(RoleID)) return;
        RoleID rid = *reinterpret_cast<const RoleID*>(data);
        LOG_DEBUG("SaveRole req roleID=%llu", rid);
        // 实际实现：将社会关系数据序列化落库
    }

    void OnFriendUpdate(ConnID fromConn, const char* data, uint16_t len)
    {
        LOG_DEBUG("FriendUpdate len=%d", len);
        // 解析好友更新，广播给在线好友
    }

    // 推送离线消息
    void PushOfflineMsg(RoleID toID, uint16_t msgID,
                        const char* data, uint16_t len)
    {
        OfflineMsg msg;
        msg.toID  = toID;
        msg.msgID = msgID;
        msg.data.assign(data, data + len);
        m_offlineMsgs[toID].push_back(std::move(msg));
    }

    TcpServer  m_server;
    TcpClient  m_superClient;
    uint32_t   m_hbSeq = 0;
    std::unordered_map<RoleID, std::shared_ptr<SessionRole>> m_roles;
    std::unordered_map<RoleID, std::vector<OfflineMsg>>      m_offlineMsgs;
};

// 占位结构（补充协议用）
#pragma pack(push,1)
struct Msg_SES_LoadRoleRsp { uint64_t roleID; int32_t code; };
#pragma pack(pop)
