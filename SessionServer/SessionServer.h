/**
 * @file    SessionServer.h
 * @brief  会话服务器 —— 社会关系数据管理、离线消息处理
 *
 * ## 职责
 * - 管理好友列表 / 黑名单 / 公会 / 队伍等社会关系数据
 * - 存储和转发离线消息（离线玩家上线时推送）
 * - 向 SuperServer 注册并维持心跳
 *
 * ## 依赖关系
 * - 依赖 SuperServer（通过 TcpClient 注册）
 * - 被 RecordServer / SceneServer / LoggerServer 等连接
 *
 * ## 通信关系
 * - 与 RecordServer 互通（加载/保存社会关系数据）
 * - 与 GlobalServer / ZoneServer 互通（跨区社交数据）
 */

#pragma once
#include "../sdk/net/TcpServer.h"
#include "../sdk/net/TcpClient.h"
#include "../sdk/util/MsgDispatcher.h"
#include "../sdk/util/UserBase.h"
#include "../sdk/log/Logger.h"
#include "../sdk/timer/TimerMgr.h"
#include "../protocal/InternalMsg.h"
#include <unordered_map>
#include <vector>
#include <string>

/**
 * @brief 用户社会关系数据
 *
 * 持久化存储，用户上线时从 DB 加载到内存，下线时写回。
 */
struct SocialData
{
    UserID               userID;              /**< 用户 ID */
    std::vector<UserID>  friends;             /**< 好友列表（互相关注的其他用户 ID 集合） */
    std::vector<UserID>  blackList;           /**< 黑名单（被屏蔽的用户 ID 集合） */
    uint64_t             guildID = 0;         /**< 公会 ID（0=无公会） */
    uint32_t             teamID  = 0;         /**< 当前队伍 ID（0=未组队） */
};

/**
 * @brief SessionServer 中维护的用户对象
 *
 * 继承 IUser，额外持有 SocialData。
 */
class SessionUser : public IUser
{
public:
    explicit SessionUser(const UserBase& base) : IUser(base) {}
    SocialData& Social() { return m_social; }  /**< 获取社会关系数据（可写） */
private:
    SocialData m_social;  /**< 社会关系数据 */
};

/**
 * @brief 离线消息
 *
 * 当目标用户不在线时，消息缓存在 SessionServer 内存中，
 * 用户下次上线时批量推送。
 */
struct OfflineMsg
{
    UserID   toID;              /**< 目标用户 ID */
    uint16_t msgID;             /**< 原始消息协议号 */
    std::vector<char> data;     /**< 消息体（二进制） */
};

/**
 * @brief SessionServer 核心类
 *
 * 单进程运行，同时作为 TcpServer（被其他服务器连接）和 TcpClient（连接 SuperServer）。
 */
class SessionServer : public INetCallback
{
public:
    SessionServer() : m_server(this), m_superClient(this) {}

    /**
     * @brief 初始化 SessionServer
     * @param ip       自身监听 IP
     * @param port     自身监听端口
     * @param superIP  SuperServer IP
     * @param superPort SuperServer 端口
     * @return 成功返回 true
     */
    bool Init(const std::string& ip, uint16_t port,
              const std::string& superIP, uint16_t superPort)
    {
        Logger::Instance().SetServerName("SessionServer");
        LOG_INFO("SessionServer starting on %s:%d", ip.c_str(), port);
        if (!m_server.Start(ip, port)) { LOG_FATAL("Start failed"); return false; }

        if (!m_superClient.Connect(superIP, superPort))
        { LOG_WARN("Cannot connect to SuperServer"); }

        RegisterHandlers();

        // 0.5 秒后首次注册到 SuperServer
        TimerMgr::Instance().Register(500, 0, [this]{ RegisterToSuper(); });
        // 每 10 秒发送心跳
        TimerMgr::Instance().Register(10000, 10000, [this]{ SendHeartbeat(); });
        LOG_INFO("SessionServer started.");
        return true;
    }

    /** @brief 主循环 */
    void Run()
    {
        while (true)
        {
            m_superClient.Poll(0);
            m_server.Poll(10);
            TimerMgr::Instance().Update();
        }
    }

    // ============================================================
    //  INetCallback 实现
    // ============================================================
    void OnConnect(ConnID id) override { LOG_INFO("InnerConn connected=%u", id); }
    void OnDisconnect(ConnID id) override { LOG_INFO("InnerConn disconnected=%u", id); }
    void OnMessage(ConnID id, uint16_t msgID, const char* data, uint16_t len) override
    {
        MsgDispatcher::Instance().Dispatch(id, msgID, data, len);
    }

private:
    /**
     * @brief 注册消息处理函数
     *
     * SES_LOAD_USER_REQ → OnLoadUserReq（加载社会关系）
     * SES_SAVE_USER_REQ → OnSaveUserReq（保存社会关系）
     * SES_FRIEND_UPDATE → OnFriendUpdate（好友变更同步）
     */
    void RegisterHandlers()
    {
        auto& d = MsgDispatcher::Instance();
        d.Register((uint16_t)InternalMsgID::S2S_HEARTBEAT_ACK,
            [](uint32_t, const char*, uint16_t){ });  /**< 忽略心跳应答（空操作） */
        d.Register((uint16_t)InternalMsgID::SES_LOAD_USER_REQ,
            [this](uint32_t c, const char* d, uint16_t l){ OnLoadUserReq(c, d, l); });  /**< 加载用户社会关系数据 */
        d.Register((uint16_t)InternalMsgID::SES_SAVE_USER_REQ,
            [this](uint32_t c, const char* d, uint16_t l){ OnSaveUserReq(c, d, l); });  /**< 保存用户社会关系数据 */
        d.Register((uint16_t)InternalMsgID::SES_FRIEND_UPDATE,
            [this](uint32_t c, const char* d, uint16_t l){ OnFriendUpdate(c, d, l); });  /**< 好友变更同步处理 */
    }

    /** @brief 向 SuperServer 发送注册消息 */
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

    /** @brief 向 SuperServer 发送心跳 */
    void SendHeartbeat()
    {
        Msg_S2S_Heartbeat hb{};
        hb.seq       = ++m_hbSeq;
        hb.timestamp = TimerMgr::NowMs();
        m_superClient.SendMsg((uint16_t)InternalMsgID::S2S_HEARTBEAT,
                               reinterpret_cast<char*>(&hb), sizeof(hb));
    }

    /**
     * @brief 处理社会关系数据加载请求
     *
     * 若用户不在 m_users 中则新建一个 SessionUser 对象。
     */
    void OnLoadUserReq(ConnID fromConn, const char* data, uint16_t len)
    {
        if (len < sizeof(UserID)) return;
        UserID uid = *reinterpret_cast<const UserID*>(data);
        LOG_DEBUG("LoadUser req userID=%llu", uid);

        auto it = m_users.find(uid);
        if (it == m_users.end())
        {
            UserBase base; base.userID = uid;
            m_users.emplace(uid, std::make_shared<SessionUser>(base));
        }

        m_server.SendMsg(fromConn, (uint16_t)InternalMsgID::SES_LOAD_USER_RSP,
                         data, len);
    }

    /** @brief 处理社会关系数据保存请求（当前为占位实现） */
    void OnSaveUserReq(ConnID /*fromConn*/, const char* data, uint16_t len)
    {
        if (len < sizeof(UserID)) return;
        UserID uid = *reinterpret_cast<const UserID*>(data);
        LOG_DEBUG("SaveUser req userID=%llu", uid);
        // TODO: 将 SocialData 序列化写入 DB
    }

    /** @brief 处理好友更新（当前为占位实现） */
    void OnFriendUpdate(ConnID /*fromConn*/, const char* /*data*/, uint16_t len)
    {
        LOG_DEBUG("FriendUpdate len=%d", len);
        // TODO: 解析好友变更，广播给在线好友
    }

    /**
     * @brief 推送离线消息
     *
     * 目标用户不在线时将消息缓存在 m_offlineMsgs 中。
     * @param toID  目标用户 ID
     * @param msgID 消息协议号
     * @param data  消息体
     * @param len   消息体长度
     */
    void PushOfflineMsg(UserID toID, uint16_t msgID,
                        const char* data, uint16_t len)
    {
        OfflineMsg msg;
        msg.toID  = toID;
        msg.msgID = msgID;
        msg.data.assign(data, data + len);
        m_offlineMsgs[toID].push_back(std::move(msg));
    }

    TcpServer  m_server;         /**< 内部服务器连接监听 */
    TcpClient  m_superClient;    /**< 到 SuperServer 的连接 */
    uint32_t   m_hbSeq = 0;      /**< 心跳序列号 */

    /** @brief 在线用户社会关系数据：userID → SessionUser */
    std::unordered_map<UserID, std::shared_ptr<SessionUser>> m_users;
    /** @brief 离线消息队列：userID → 消息列表 */
    std::unordered_map<UserID, std::vector<OfflineMsg>>      m_offlineMsgs;
};
