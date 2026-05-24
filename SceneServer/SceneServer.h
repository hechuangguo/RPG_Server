/**
 * @file    SceneServer.h
 * @brief  场景服务器 —— 在线用户数据管理、地图逻辑、Lua 脚本执行
 *
 * ## 职责
 * - 管理用户在线状态与地图实例
 * - 处理客户端游戏消息（移动、聊天、技能、心跳）
 * - 内嵌 Lua 虚拟机执行游戏脚本（NPC、任务、技能等）
 * - 与 AOIServer 协同管理视野
 *
 * ## 依赖关系
 * - 依赖 SuperServer / SessionServer / RecordServer / AOIServer / GatewayServer
 * - 可选连接 GlobalServer / ZoneServer
 * - 支持多进程负载均衡（多 SceneServer 承载不同地图）
 *
 * ## Lua 集成
 * SceneServer 启动时加载 script/scene/init.lua，注册 C++ 函数到 Lua：
 * - log_info(msg)     : 输出日志
 * - send_to_user(userID, msgID, data) : 向客户端发送消息
 *
 * Lua 回调约定：
 * - OnUserEnter(userID, mapID) : 用户进入场景
 * - OnUserLeave(userID)         : 用户离开场景
 * - OnTick(nowMs)               : 每帧回调
 * - OnSkillReq(connID, data)    : 技能请求
 * - OnMsg_XXXX(connID, data)    : 自定义消息处理（XXXX 为十六进制 msgID）
 */

#pragma once
#include "../sdk/net/TcpServer.h"
#include "../sdk/net/TcpClient.h"
#include "../sdk/util/UserBase.h"
#include "../sdk/util/MsgDispatcher.h"
#include "../sdk/util/SceneInfoLoader.h"
#include "../sdk/util/ConfigLoader.h"
#include "../sdk/log/Logger.h"
#include "../sdk/timer/TimerMgr.h"
#include "../protocal/InternalMsg.h"
#include "../common/ClientMsg.h"
#include <unordered_map>
#include <vector>
#include <string>
#include <memory>

// Lua C API
extern "C"
{
    #include "lua.h"
    #include "lualib.h"
    #include "lauxlib.h"
}

/**
 * @brief SceneServer 中的在线用户对象
 *
 * 继承 IUser，额外持有与 GatewayServer 关联的连接信息。
 */
class SceneUser : public IUser
{
public:
    explicit SceneUser(const UserBase& base) : IUser(base) {}
    ConnID   gatewayConnID     = INVALID_CONN_ID;  /**< 对应 GatewayServer 的内部连接 */
    uint32_t gatewayClientConn = 0;                /**< 在 GatewayServer 的客户端连接 ID */
};

/**
 * @brief 地图实例（运行在 SceneServer 进程中）
 */
struct MapInstance
{
    uint32_t                   mapID;      /**< 地图唯一 ID */
    std::string                mapName;    /**< 地图名称 */
    uint32_t                   maxPlayer;  /**< 最大容纳玩家数 */
    std::vector<UserID>        players;    /**< 当前在本地图内的用户 ID 列表 */
};

/**
 * @brief SceneServer 核心类
 *
 * 连接最复杂的服务器，与几乎所有其他服务器有通信关系。
 */
class SceneServer : public INetCallback
{
public:
    static SceneServer* Instance() { return s_instance; }

    SceneServer()
        : m_server(this)
        , m_superClient(this)
        , m_sessionClient(this)
        , m_recordClient(this)
        , m_aoiClient(this)
        , m_gatewayClient(this)
        , m_globalClient(this)
        , m_zoneClient(this)
        , m_lua(nullptr)
        , m_sceneID(0)
    {}
    ~SceneServer() { if (m_lua) lua_close(m_lua); }

    /**
     * @brief 初始化 SceneServer
     * @param ip        监听 IP
     * @param port      监听端口
     * @param cfg       全局配置
     * @param sceneInfo 场景地图配置
     * @return 成功返回 true
     */
    bool Init(const std::string& ip, uint16_t port,
              const ServerConfig& cfg, const SceneServerInfo& sceneInfo)
    {
        Logger::Instance().SetServerName("SceneServer");
        m_sceneID = sceneInfo.sceneID;
        s_instance = this;
        if (!m_server.Start(ip, port)) { LOG_FATAL("SceneServer start failed"); return false; }

        // ── 连接各依赖服务器 ──
        m_superClient.Connect(cfg.superIP,       (uint16_t)cfg.superPort);
        m_sessionClient.Connect("127.0.0.1",     (uint16_t)cfg.sessionPort);
        m_recordClient.Connect("127.0.0.1",      (uint16_t)cfg.recordPort);
        m_aoiClient.Connect("127.0.0.1",         (uint16_t)cfg.aoiPort);
        m_gatewayClient.Connect("127.0.0.1",     (uint16_t)(cfg.gatewayPort + 10000));
        m_listenPort = port;

        // ── 加载地图配置 ──
        for (auto& mc : sceneInfo.maps)
        {
            MapInstance mi;
            mi.mapID     = mc.mapID;
            mi.mapName   = mc.mapName;
            mi.maxPlayer = mc.maxPlayer;
            m_maps[mc.mapID] = mi;
            LOG_INFO("Map loaded: id=%u name=%s", mc.mapID, mc.mapName.c_str());
        }

        InitLua();      /**< 初始化 Lua 虚拟机 */
        RegisterHandlers();

        /** @brief 定时向 SuperServer 发起注册，确保服务发现 */
        TimerMgr::Instance().Register(500,   0,    [this]{ RegisterToSuper(); });
        /** @brief 每 10 秒发送一次心跳，维持与 SuperServer 的连接存活 */
        TimerMgr::Instance().Register(10000, 10000,[this]{ SendHeartbeat(); });
        /** @brief 每 1 秒触发一次逻辑 Tick，驱动用户 OnTick 与 Lua OnTick */
        TimerMgr::Instance().Register(1000,  1000, [this]{ OnTick(); });

        LOG_INFO("SceneServer %u started on %s:%d", m_sceneID, ip.c_str(), port);
        return true;
    }

    /** @brief 主循环：轮询所有连接的 epoll + 驱动定时器 */
    void Run()
    {
        while (true)
        {
            m_superClient.Poll(0);
            m_sessionClient.Poll(0);
            m_recordClient.Poll(0);
            m_aoiClient.Poll(0);
            m_gatewayClient.Poll(0);
            m_globalClient.Poll(0);
            m_zoneClient.Poll(0);
            m_server.Poll(10);
            TimerMgr::Instance().Update();
        }
    }

    void OnConnect(ConnID /*id*/)    override {}
    void OnDisconnect(ConnID id) override { LOG_WARN("SceneServer conn lost=%u", id); }
    void OnMessage(ConnID id, uint16_t msgID, const char* data, uint16_t len) override
    {
        MsgDispatcher::Instance().Dispatch(id, msgID, data, len);
    }

private:
    /**
     * @brief 初始化 Lua 虚拟机
     *
     * 打开标准库 → 注册 C++ 函数 → 加载 scene/init.lua。
     */
    void InitLua()
    {
        m_lua = luaL_newstate();
        luaL_openlibs(m_lua);
        lua_register(m_lua, "log_info",     LuaLogInfo);
        lua_register(m_lua, "send_to_user", LuaSendToUser);
        luaL_dostring(m_lua, "package.path = package.path .. ';../script/?.lua'");
        if (luaL_dofile(m_lua, "../script/scene/init.lua") != LUA_OK)
            LOG_WARN("Lua init.lua load failed: %s", lua_tostring(m_lua, -1));
    }

    /** @brief Lua → C++: log_info(msg) —— 输出日志 */
    static int LuaLogInfo(lua_State* L)
    {
        const char* msg = luaL_checkstring(L, 1);
        LOG_INFO("[Lua] %s", msg);
        return 0;
    }

    /**
     * @brief Lua → C++: send_to_user(userID, msgID, data)
     *
     * Lua 脚本通过此函数向指定用户发送消息。
     * 根据 userID 查找在线用户，若找到且拥有有效的 Gateway 连接，
     * 则通过 SendToClient 将消息转发至客户端。
     */
    static int LuaSendToUser(lua_State* L)
    {
        UserID userID = (UserID)luaL_checkinteger(L, 1);
        uint16_t msgID = (uint16_t)luaL_checkinteger(L, 2);
        size_t len = 0;
        const char* data = luaL_optlstring(L, 3, "", &len);
        auto* self = SceneServer::Instance();
        if (!self) return 0;
        auto user = self->FindUser(userID);
        if (!user || user->gatewayClientConn == 0) return 0;
        self->SendToClient(user->gatewayClientConn, msgID, data, (uint16_t)len);
        return 0;
    }

    void RegisterHandlers()
    {
        auto& d = MsgDispatcher::Instance();
        /** @brief 注册用户进入场景消息处理 */
        d.Register((uint16_t)InternalMsgID::SCE_USER_ENTER_REQ,
            [this](uint32_t c, const char* d, uint16_t l){ OnUserEnter(c, d, l); });
        /** @brief 注册用户离开场景消息处理 */
        d.Register((uint16_t)InternalMsgID::SCE_USER_LEAVE,
            [this](uint32_t c, const char* d, uint16_t l){ OnUserLeave(c, d, l); });
        /** @brief 注册 Gateway 转发的客户端消息处理 */
        d.Register((uint16_t)InternalMsgID::GW_CLIENT_MSG,
            [this](uint32_t c, const char* d, uint16_t l){ OnClientMsg(c, d, l); });
        /** @brief 注册 AOI 视野变化通知处理 */
        d.Register((uint16_t)InternalMsgID::AOI_VIEW_NOTIFY,
            [this](uint32_t c, const char* d, uint16_t l){ OnViewNotify(c, d, l); });
    }

    /**
     * @brief 用户进入场景
     *
     * 创建 SceneUser → 加入地图 → 通知 AOI → 回包 GatewayServer → 调用 Lua OnUserEnter。
     */
    void OnUserEnter(ConnID /*fromConn*/, const char* data, uint16_t len)
    {
        if (len < sizeof(Msg_SCE_UserEnterReq)) return;
        const auto* req = reinterpret_cast<const Msg_SCE_UserEnterReq*>(data);
        LOG_INFO("UserEnter: userID=%llu mapID=%u clientConn=%u",
                 req->userID, req->mapID, req->gatewayClientConnID);

        uint32_t mapID = req->mapID ? req->mapID : 1001;
        auto it = m_maps.find(mapID);
        if (it == m_maps.end())
        {
            LOG_WARN("Map %u not found on SceneServer %u", mapID, m_sceneID);
            SendUserEnterRsp(req, -1);
            return;
        }

        UserBase base;
        base.userID   = req->userID;
        base.name     = req->name;
        base.level    = req->level;
        base.vocation = req->vocation;
        base.sex      = req->sex;
        base.mapID    = mapID;
        base.posX     = req->x;
        base.posY     = req->y;
        base.posZ     = req->z;
        base.hp       = req->hp;
        base.maxHP    = req->maxHP;
        base.mp       = req->mp;
        base.maxMP    = req->maxMP;
        base.gold     = req->gold;

        auto user = std::make_shared<SceneUser>(base);
        user->gatewayClientConn = req->gatewayClientConnID;
        user->SetState(UserState::ONLINE);
        m_users[req->userID] = user;
        it->second.players.push_back(req->userID);

        Msg_AOI_Move aoi{};
        aoi.entityID = req->userID;
        aoi.mapID    = mapID;
        aoi.x = req->x;
        aoi.y = req->y;
        aoi.z = req->z;
        m_aoiClient.SendMsg((uint16_t)InternalMsgID::AOI_ENTER_REQ,
                             reinterpret_cast<char*>(&aoi), sizeof(aoi));

        NotifyExistingPlayersOnEnter(*user);
        SendUserEnterRsp(req, 0);
        CallLuaOnEnter(req->userID, mapID);
    }

    void SendUserEnterRsp(const Msg_SCE_UserEnterReq* req, int32_t code)
    {
        Msg_SCE_UserEnterRsp rsp{};
        rsp.code                = code;
        rsp.userID              = req->userID;
        rsp.gatewayClientConnID = req->gatewayClientConnID;
        rsp.mapID               = req->mapID ? req->mapID : 1001;
        m_superClient.SendMsg((uint16_t)InternalMsgID::SCE_USER_ENTER_RSP,
                              reinterpret_cast<char*>(&rsp), sizeof(rsp));
    }

    void NotifyExistingPlayersOnEnter(const SceneUser& entering)
    {
        const auto& base = entering.Base();
        Msg_S2C_SpawnEntity spawn{};
        spawn.entityID   = base.userID;
        spawn.level      = base.level;
        spawn.x          = base.posX;
        spawn.y          = base.posY;
        spawn.z          = base.posZ;
        spawn.dir        = 0.f;
        spawn.entityType = 0;
        strncpy(spawn.name, base.name.c_str(), sizeof(spawn.name) - 1);

        for (auto& [rid, user] : m_users)
        {
            if (rid == base.userID) continue;
            if (user->Base().mapID != base.mapID) continue;
            if (user->gatewayClientConn == 0) continue;

            SendToClient(user->gatewayClientConn,
                         (uint16_t)ClientMsgID::S2C_SPAWN_ENTITY,
                         reinterpret_cast<char*>(&spawn), sizeof(spawn));

            Msg_S2C_SpawnEntity other{};
            other.entityID   = user->Base().userID;
            other.level      = user->Base().level;
            other.x          = user->Base().posX;
            other.y          = user->Base().posY;
            other.z          = user->Base().posZ;
            other.entityType = 0;
            strncpy(other.name, user->Base().name.c_str(), sizeof(other.name) - 1);
            SendToClient(entering.gatewayClientConn,
                         (uint16_t)ClientMsgID::S2C_SPAWN_ENTITY,
                         reinterpret_cast<char*>(&other), sizeof(other));
        }
    }

    /** @brief 用户离开场景（通知 AOI → 保存到 RecordServer → 调用 Lua → 清理内存） */
    void OnUserLeave(ConnID /*fromConn*/, const char* data, uint16_t len)
    {
        if (len < sizeof(UserID)) return;
        UserID uid = *reinterpret_cast<const UserID*>(data);
        auto it = m_users.find(uid);
        if (it == m_users.end()) return;

        m_aoiClient.SendMsg((uint16_t)InternalMsgID::AOI_LEAVE_REQ,
                             reinterpret_cast<const char*>(&uid), sizeof(uid));
        m_recordClient.SendMsg((uint16_t)InternalMsgID::REC_SAVE_USER_REQ,
                                reinterpret_cast<const char*>(&uid), sizeof(uid));
        CallLuaOnLeave(uid);
        m_users.erase(it);
        LOG_INFO("UserLeave: userID=%llu", uid);
    }

    /** @brief 处理 GatewayServer 转发来的客户端消息 */
    void OnClientMsg(ConnID /*fromConn*/, const char* data, uint16_t len)
    {
        if (len < sizeof(Msg_GW_ClientMsg)) return;
        const auto* hdr = reinterpret_cast<const Msg_GW_ClientMsg*>(data);
        const char* body = data + sizeof(Msg_GW_ClientMsg);
        uint16_t bodyLen = hdr->dataLen;
        LOG_DEBUG("ClientMsg: connID=%u msgID=0x%04X", hdr->clientConnID, hdr->msgID);
        HandleClientMsg(hdr->clientConnID, hdr->msgID, body, bodyLen);
    }

    /**
     * @brief 处理 AOI 视野变化通知
     *
     * 根据消息长度区分两种场景：
     * - 若 len == sizeof(Msg_AOI_Move)：为移动坐标更新，直接同步实体位置并向同地图广播移动通知；
     * - 否则为视野进入/离开通知，解析 entityID + enter 标志位：
     *   - enter=true：向视野内其他用户广播 SpawnEntity（新实体出现）；
     *   - enter=false：向视野内其他用户广播 DespawnEntity（实体消失）。
     */
    void OnViewNotify(ConnID /*fromConn*/, const char* data, uint16_t len)
    {
        if (len == sizeof(Msg_AOI_Move))
        {
            const auto* move = reinterpret_cast<const Msg_AOI_Move*>(data);
            auto it = m_users.find(move->entityID);
            if (it != m_users.end())
            {
                it->second->Base().posX = move->x;
                it->second->Base().posY = move->y;
                it->second->Base().posZ = move->z;
            }

            Msg_S2C_MoveNotify notify{};
            notify.userID   = move->entityID;
            notify.x        = move->x;
            notify.y        = move->y;
            notify.z        = move->z;
            notify.dir      = move->dir;
            notify.moveType = 0;
            BroadcastToMap(move->mapID, move->entityID,
                           (uint16_t)ClientMsgID::S2C_MOVE_NOTIFY,
                           reinterpret_cast<char*>(&notify), sizeof(notify));
            return;
        }

        if (len < sizeof(uint64_t) + 1) return;
        uint64_t entityID = 0;
        memcpy(&entityID, data, sizeof(uint64_t));
        bool enter = data[sizeof(uint64_t)] != 0;

        auto it = m_users.find(entityID);
        if (it == m_users.end()) return;
        const auto& user = it->second;
        if (user->gatewayClientConn == 0) return;

        if (enter)
        {
            Msg_S2C_SpawnEntity spawn{};
            spawn.entityID   = entityID;
            spawn.level      = user->Base().level;
            spawn.x          = user->Base().posX;
            spawn.y          = user->Base().posY;
            spawn.z          = user->Base().posZ;
            spawn.entityType = 0;
            strncpy(spawn.name, user->Base().name.c_str(), sizeof(spawn.name) - 1);
            BroadcastToMap(user->Base().mapID, entityID,
                           (uint16_t)ClientMsgID::S2C_SPAWN_ENTITY,
                           reinterpret_cast<char*>(&spawn), sizeof(spawn));
        }
        else
        {
            Msg_S2C_DespawnEntity despawn{};
            despawn.entityID = entityID;
            BroadcastToMap(user->Base().mapID, entityID,
                           (uint16_t)ClientMsgID::S2C_DESPAWN_ENTITY,
                           reinterpret_cast<char*>(&despawn), sizeof(despawn));
        }
    }

    /**
     * @brief 客户端消息分发路由
     *
     * 采用 switch-case 路由策略：已知协议号直接映射到对应的处理函数，
     * 未知协议号统一委派给 Lua 脚本处理（OnMsg_XXXX）。
     * 路由优先级：C++ 原生处理 > Lua 脚本扩展处理。
     */
    void HandleClientMsg(uint32_t clientConnID, uint16_t msgID,
                         const char* data, uint16_t len)
    {
        using CID = ClientMsgID;
        switch ((CID)msgID)
        {
        case CID::C2S_MOVE_REQ:   OnMoveReq(clientConnID, data, len); break;
        case CID::C2S_CHAT_REQ:   OnChatReq(clientConnID, data, len); break;
        case CID::C2S_SKILL_REQ:  OnSkillReq(clientConnID, data, len); break;
        case CID::C2S_HEARTBEAT:  OnHeartbeatReq(clientConnID, data, len); break;
        default:                  CallLuaMsgHandler(clientConnID, msgID, data, len);
        }
    }

    /**
     * @brief 处理移动请求：坐标验证 → 位置更新 → 通知 AOI
     *
     * 移动验证流程：
     * 1. 根据消息中的 userID 查找在线用户，不存在则忽略；
     * 2. 将客户端上报的坐标 (x, y, z) 更新到用户的基础数据中；
     * 3. 构造 AOI 移动消息并转发至 AOIServer，由 AOIServer 计算视野变化
     *    并回传 OnViewNotify，触发同地图其他客户端的坐标同步。
     *
     * @note 当前未做坐标合法性校验（如移动速度检测），后续可扩展。
     */
    void OnMoveReq(uint32_t /*clientConnID*/, const char* data, uint16_t len)
    {
        if (len < sizeof(Msg_C2S_MoveReq)) return;
        const auto* req = reinterpret_cast<const Msg_C2S_MoveReq*>(data);
        auto it = m_users.find(req->userID);
        if (it == m_users.end()) return;
        auto& user = it->second;
        user->Base().posX = req->x;
        user->Base().posY = req->y;
        user->Base().posZ = req->z;

        Msg_AOI_Move aoi{};
        aoi.entityID = req->userID;
        aoi.mapID    = user->Base().mapID;
        aoi.x = req->x; aoi.y = req->y; aoi.z = req->z; aoi.dir = req->dir;
        m_aoiClient.SendMsg((uint16_t)InternalMsgID::AOI_MOVE_REQ,
                             reinterpret_cast<char*>(&aoi), sizeof(aoi));
    }

    /** @brief 处理聊天请求：广播给地图内所有玩家 */
    void OnChatReq(uint32_t clientConnID, const char* data, uint16_t len)
    {
        if (len < sizeof(Msg_C2S_Chat)) return;
        const auto* req = reinterpret_cast<const Msg_C2S_Chat*>(data);
        auto user = FindUserByClientConn(clientConnID);
        if (!user) return;

        Msg_S2C_Chat notify{};
        notify.fromID = user->GetID();
        notify.channel = req->channel;
        snprintf(notify.fromName, sizeof(notify.fromName), "%s", user->GetName());
        snprintf(notify.content, sizeof(notify.content), "%s", req->content);
        BroadcastToMap(user->Base().mapID, user->GetID(),
                       (uint16_t)ClientMsgID::S2C_CHAT_NOTIFY,
                       reinterpret_cast<char*>(&notify), sizeof(notify));
    }

    /** @brief 处理技能请求：委派 Lua 处理 */
    void OnSkillReq(uint32_t clientConnID, const char* data, uint16_t len)
    {
        LOG_DEBUG("SkillReq from conn=%u", clientConnID);
        CallLuaSkillHandler(clientConnID, data, len);
    }

    /** @brief 处理心跳请求：回包服务器时间 */
    void OnHeartbeatReq(uint32_t clientConnID, const char* data, uint16_t len)
    {
        Msg_S2C_Heartbeat rsp{};
        if (len >= sizeof(Msg_C2S_Heartbeat))
            rsp.seq = reinterpret_cast<const Msg_C2S_Heartbeat*>(data)->seq;
        rsp.serverTime = TimerMgr::NowMs();
        SendToClient(clientConnID, (uint16_t)ClientMsgID::S2C_HEARTBEAT,
                     reinterpret_cast<char*>(&rsp), sizeof(rsp));
    }

    /**
     * @brief 向客户端发送消息（通过 GatewayServer 转发）
     *
     * 包格式：[clientConnID(4)][msgID(2)][data...]
     */
    void SendToClient(uint32_t clientConnID, uint16_t msgID, const char* data, uint16_t len)
    {
        std::vector<char> buf(sizeof(uint32_t) + sizeof(uint16_t) + len);
        memcpy(buf.data(), &clientConnID, sizeof(uint32_t));
        memcpy(buf.data() + sizeof(uint32_t), &msgID, sizeof(uint16_t));
        if (len > 0) memcpy(buf.data() + sizeof(uint32_t) + sizeof(uint16_t), data, len);
        m_gatewayClient.SendMsg((uint16_t)InternalMsgID::GW_SEND_TO_CLIENT,
                                 buf.data(), (uint16_t)buf.size());
    }

    /**
     * @brief 广播消息给指定地图内的所有用户（可排除指定用户）
     *
     * 广播排除逻辑：
     * 1. 遍历所有在线用户 m_users；
     * 2. 跳过不在目标 mapID 中的用户（mapID==0 时广播所有地图）；
     * 3. 跳过 excludeUserID 指定的用户（通常为消息发起者，避免自己收到自己的广播）；
     * 4. 跳过 gatewayClientConn==0 的用户（连接无效，无法发送）；
     * 5. 对剩余用户逐一调用 SendToClient 转发消息。
     *
     * @param mapID          目标地图 ID，0 表示所有地图
     * @param excludeUserID  需要排除的用户 ID
     * @param msgID          消息协议号
     * @param data           消息体指针
     * @param len            消息体长度
     */
    void BroadcastToMap(uint32_t mapID, UserID excludeUserID,
                        uint16_t msgID, const char* data, uint16_t len)
    {
        for (auto& [uid, user] : m_users)
        {
            if (mapID != 0 && user->Base().mapID != mapID) continue;
            if (uid == excludeUserID) continue;
            if (user->gatewayClientConn == 0) continue;
            SendToClient(user->gatewayClientConn, msgID, data, len);
        }
    }

    std::shared_ptr<SceneUser> FindUser(UserID userID)
    {
        auto it = m_users.find(userID);
        return it != m_users.end() ? it->second : nullptr;
    }

    std::shared_ptr<SceneUser> FindUserByClientConn(uint32_t clientConnID)
    {
        for (auto& [uid, user] : m_users)
            if (user->gatewayClientConn == clientConnID) return user;
        return nullptr;
    }

    /** @brief Lua 回调：用户进入场景 */
    void CallLuaOnEnter(UserID userID, uint32_t mapID)
    {
        if (!m_lua) return;
        lua_getglobal(m_lua, "OnUserEnter");
        if (lua_isfunction(m_lua, -1))
        {
            lua_pushinteger(m_lua, (lua_Integer)userID);
            lua_pushinteger(m_lua, (lua_Integer)mapID);
            lua_pcall(m_lua, 2, 0, 0);
        }
        else lua_pop(m_lua, 1);
    }

    /** @brief Lua 回调：用户离开场景 */
    void CallLuaOnLeave(UserID userID)
    {
        if (!m_lua) return;
        lua_getglobal(m_lua, "OnUserLeave");
        if (lua_isfunction(m_lua, -1))
        {
            lua_pushinteger(m_lua, (lua_Integer)userID);
            lua_pcall(m_lua, 1, 0, 0);
        }
        else lua_pop(m_lua, 1);
    }

    /**
     * @brief Lua 回调：通用消息处理（OnMsg_XXXX）
     *
     * Lua 消息转发机制：
     * 1. 根据客户端协议号 msgID 格式化 Lua 函数名 "OnMsg_XXXX"（XXXX 为十六进制）；
     * 2. 通过 lua_getglobal 在 Lua 全局表中查找该函数；
     * 3. 若函数存在，将 connID（整数）和 data（二进制字符串）压栈并调用；
     * 4. 若函数不存在，弹出栈顶的 nil 值，静默忽略。
     *
     * 此机制允许游戏策划/脚本开发者在不修改 C++ 代码的情况下，
     * 通过在 Lua 脚本中定义 OnMsg_XXXX 函数来扩展新的消息处理逻辑。
     */
    void CallLuaMsgHandler(uint32_t connID, uint16_t msgID, const char* data, uint16_t len)
    {
        if (!m_lua) return;
        char funcName[32];
        snprintf(funcName, sizeof(funcName), "OnMsg_%04X", msgID);
        lua_getglobal(m_lua, funcName);
        if (lua_isfunction(m_lua, -1))
        {
            lua_pushinteger(m_lua, connID);
            lua_pushlstring(m_lua, data, len);
            lua_pcall(m_lua, 2, 0, 0);
        }
        else lua_pop(m_lua, 1);
    }

    /** @brief Lua 回调：技能请求 */
    void CallLuaSkillHandler(uint32_t connID, const char* data, uint16_t len)
    {
        if (!m_lua) return;
        lua_getglobal(m_lua, "OnSkillReq");
        if (lua_isfunction(m_lua, -1))
        {
            lua_pushinteger(m_lua, connID);
            lua_pushlstring(m_lua, data, len);
            lua_pcall(m_lua, 2, 0, 0);
        }
        else lua_pop(m_lua, 1);
    }

    /** @brief 每帧 Tick（驱动用户 OnTick + Lua OnTick） */
    void OnTick()
    {
        uint64_t now = TimerMgr::NowMs();
        for (auto& [uid, user] : m_users) user->OnTick(now);
        if (m_lua)
        {
            lua_getglobal(m_lua, "OnTick");
            if (lua_isfunction(m_lua, -1))
            {
                lua_pushinteger(m_lua, (lua_Integer)now);
                lua_pcall(m_lua, 1, 0, 0);
            }
            else lua_pop(m_lua, 1);
        }
    }

    void RegisterToSuper()
    {
        Msg_S2S_Register reg{};
        reg.serverType = (uint8_t)SubServerType::SCENE;
        reg.serverID   = m_sceneID;
        strncpy(reg.ip, "127.0.0.1", sizeof(reg.ip));
        reg.port       = m_listenPort;
        m_superClient.SendMsg((uint16_t)InternalMsgID::S2S_REGISTER_REQ,
                               reinterpret_cast<char*>(&reg), sizeof(reg));
    }

    void SendHeartbeat()
    {
        Msg_S2S_Heartbeat hb{}; hb.seq = ++m_hbSeq; hb.timestamp = TimerMgr::NowMs();
        m_superClient.SendMsg((uint16_t)InternalMsgID::S2S_HEARTBEAT,
                               reinterpret_cast<char*>(&hb), sizeof(hb));
    }

    TcpServer  m_server;          /**< 内部连接监听 */
    TcpClient  m_superClient;     /**< 到 SuperServer */
    TcpClient  m_sessionClient;   /**< 到 SessionServer */
    TcpClient  m_recordClient;    /**< 到 RecordServer */
    TcpClient  m_aoiClient;       /**< 到 AOIServer */
    TcpClient  m_gatewayClient;   /**< 到 GatewayServer */
    TcpClient  m_globalClient;    /**< 到 GlobalServer（可选） */
    TcpClient  m_zoneClient;      /**< 到 ZoneServer（可选） */
    lua_State* m_lua;             /**< Lua 虚拟机指针 */
    uint32_t   m_sceneID;         /**< 场景服务器编号 */
    uint32_t   m_hbSeq = 0;       /**< 心跳序列号 */
    uint16_t   m_listenPort = 9004;

    inline static SceneServer* s_instance = nullptr;

    /** @brief 地图实例：mapID → MapInstance */
    std::unordered_map<uint32_t, MapInstance>             m_maps;
    /** @brief 在线用户：userID → SceneUser */
    std::unordered_map<UserID, std::shared_ptr<SceneUser>> m_users;
};
