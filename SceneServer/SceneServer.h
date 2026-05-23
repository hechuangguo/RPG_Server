/**
 * @file    SceneServer.h
 * @brief  场景服务器 —— 在线角色数据管理、地图逻辑、Lua 脚本执行
 *
 * ## 职责
 * - 管理角色在线状态与地图实例
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
 * - send_to_role(rid, msgID, data) : 向客户端发送消息
 *
 * Lua 回调约定：
 * - OnRoleEnter(roleID, mapID) : 角色进入场景
 * - OnRoleLeave(roleID)         : 角色离开场景
 * - OnTick(nowMs)               : 每帧回调
 * - OnSkillReq(connID, data)    : 技能请求
 * - OnMsg_XXXX(connID, data)    : 自定义消息处理（XXXX 为十六进制 msgID）
 */

#pragma once
#include "../sdk/net/TcpServer.h"
#include "../sdk/net/TcpClient.h"
#include "../sdk/util/RoleBase.h"
#include "../sdk/util/MsgDispatcher.h"
#include "../sdk/util/SceneInfoLoader.h"
#include "../sdk/log/Logger.h"
#include "../sdk/timer/TimerMgr.h"
#include "../protocal/InternalMsg.h"
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
 * @brief SceneServer 中的在线角色对象
 *
 * 继承 IRole，额外持有与 GatewayServer 关联的连接信息。
 */
class SceneRole : public IRole
{
public:
    explicit SceneRole(const RoleBase& base) : IRole(base) {}
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
    std::vector<RoleID>        players;    /**< 当前在本地图内的角色 ID 列表 */
};

/**
 * @brief SceneServer 核心类
 *
 * 连接最复杂的服务器，与几乎所有其他服务器有通信关系。
 */
class SceneServer : public INetCallback
{
public:
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
        if (!m_server.Start(ip, port)) { LOG_FATAL("SceneServer start failed"); return false; }

        // ── 连接各依赖服务器 ──
        m_superClient.Connect(cfg.superIP,       (uint16_t)cfg.superPort);
        m_sessionClient.Connect("127.0.0.1",     (uint16_t)cfg.sessionPort);
        m_recordClient.Connect("127.0.0.1",      (uint16_t)cfg.recordPort);
        m_aoiClient.Connect("127.0.0.1",         (uint16_t)cfg.aoiPort);
        m_gatewayClient.Connect("127.0.0.1",     (uint16_t)cfg.gatewayPort);

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

        TimerMgr::Instance().Register(500,   0,    [this]{ RegisterToSuper(); });
        TimerMgr::Instance().Register(10000, 10000,[this]{ SendHeartbeat(); });
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

    void OnConnect(ConnID id)    override {}
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
        lua_register(m_lua, "send_to_role", LuaSendToRole);
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

    /** @brief Lua → C++: sendToRole(roleID, msgID, data) —— 向客户端发送消息 */
    static int LuaSendToRole(lua_State* L)
    {
        (void)L;  // TODO: 实现客户端消息发送
        return 0;
    }

    void RegisterHandlers()
    {
        auto& d = MsgDispatcher::Instance();
        d.Register((uint16_t)InternalMsgID::SCE_ROLE_ENTER_REQ,
            [this](uint32_t c, const char* d, uint16_t l){ OnRoleEnter(c, d, l); });
        d.Register((uint16_t)InternalMsgID::SCE_ROLE_LEAVE,
            [this](uint32_t c, const char* d, uint16_t l){ OnRoleLeave(c, d, l); });
        d.Register((uint16_t)InternalMsgID::GW_CLIENT_MSG,
            [this](uint32_t c, const char* d, uint16_t l){ OnClientMsg(c, d, l); });
        d.Register((uint16_t)InternalMsgID::AOI_VIEW_NOTIFY,
            [this](uint32_t c, const char* d, uint16_t l){ OnViewNotify(c, d, l); });
    }

    /**
     * @brief 角色进入场景
     *
     * 创建 SceneRole → 加入地图 → 通知 AOI → 回包 GatewayServer → 调用 Lua OnRoleEnter。
     */
    void OnRoleEnter(ConnID fromConn, const char* data, uint16_t len)
    {
        if (len < sizeof(Msg_SCE_RoleEnterReq)) return;
        const auto* req = reinterpret_cast<const Msg_SCE_RoleEnterReq*>(data);
        LOG_INFO("RoleEnter: roleID=%llu mapID=%u", req->roleID, req->mapID);

        auto it = m_maps.find(req->mapID);
        if (it == m_maps.end()) { LOG_WARN("Map %u not found on SceneServer %u", req->mapID, m_sceneID); return; }
        RoleBase base; base.roleID = req->roleID;
        base.mapID = req->mapID; base.posX = req->x; base.posZ = req->z;
        auto role = std::make_shared<SceneRole>(base);
        m_roles[req->roleID] = role;
        it->second.players.push_back(req->roleID);

        // 通知 AOI
        Msg_AOI_Move aoi{};
        aoi.entityID = req->roleID; aoi.mapID = req->mapID;
        aoi.x = req->x; aoi.z = req->z;
        m_aoiClient.SendMsg((uint16_t)InternalMsgID::AOI_ENTER_REQ,
                             reinterpret_cast<char*>(&aoi), sizeof(aoi));

        // 回包给 GatewayServer
        Msg_SCE_RoleEnterReq rsp = *req;
        m_server.SendMsg(fromConn, (uint16_t)InternalMsgID::SCE_ROLE_ENTER_RSP,
                         reinterpret_cast<char*>(&rsp), sizeof(rsp));

        CallLuaOnEnter(req->roleID, req->mapID);
    }

    /** @brief 角色离开场景（通知 AOI → 保存到 RecordServer → 调用 Lua → 清理内存） */
    void OnRoleLeave(ConnID fromConn, const char* data, uint16_t len)
    {
        if (len < sizeof(RoleID)) return;
        RoleID rid = *reinterpret_cast<const RoleID*>(data);
        auto it = m_roles.find(rid);
        if (it == m_roles.end()) return;

        m_aoiClient.SendMsg((uint16_t)InternalMsgID::AOI_LEAVE_REQ,
                             reinterpret_cast<const char*>(&rid), sizeof(rid));
        m_recordClient.SendMsg((uint16_t)InternalMsgID::REC_SAVE_ROLE_REQ,
                                reinterpret_cast<const char*>(&rid), sizeof(rid));
        CallLuaOnLeave(rid);
        m_roles.erase(it);
        LOG_INFO("RoleLeave: roleID=%llu", rid);
    }

    /** @brief 处理 GatewayServer 转发来的客户端消息 */
    void OnClientMsg(ConnID fromConn, const char* data, uint16_t len)
    {
        if (len < sizeof(Msg_GW_ClientMsg)) return;
        const auto* hdr = reinterpret_cast<const Msg_GW_ClientMsg*>(data);
        const char* body = data + sizeof(Msg_GW_ClientMsg);
        uint16_t bodyLen = hdr->dataLen;
        LOG_DEBUG("ClientMsg: connID=%u msgID=0x%04X", hdr->clientConnID, hdr->msgID);
        HandleClientMsg(hdr->clientConnID, hdr->msgID, body, bodyLen);
    }

    /** @brief 处理 AOI 视野变化通知 */
    void OnViewNotify(ConnID fromConn, const char* data, uint16_t len)
    {
        LOG_DEBUG("ViewNotify len=%d", len);
    }

    /**
     * @brief 客户端消息分发
     *
     * 已知协议直接处理，未知协议尝试委派给 Lua（OnMsg_XXXX）。
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

    /** @brief 处理移动请求：更新坐标 → 通知 AOI */
    void OnMoveReq(uint32_t clientConnID, const char* data, uint16_t len)
    {
        if (len < sizeof(Msg_C2S_MoveReq)) return;
        const auto* req = reinterpret_cast<const Msg_C2S_MoveReq*>(data);
        auto it = m_roles.find(req->roleID);
        if (it == m_roles.end()) return;
        auto& role = it->second;
        role->Base().posX = req->x;
        role->Base().posY = req->y;
        role->Base().posZ = req->z;

        Msg_AOI_Move aoi{};
        aoi.entityID = req->roleID;
        aoi.mapID    = role->Base().mapID;
        aoi.x = req->x; aoi.y = req->y; aoi.z = req->z; aoi.dir = req->dir;
        m_aoiClient.SendMsg((uint16_t)InternalMsgID::AOI_MOVE_REQ,
                             reinterpret_cast<char*>(&aoi), sizeof(aoi));
    }

    /** @brief 处理聊天请求：广播给地图内所有玩家 */
    void OnChatReq(uint32_t clientConnID, const char* data, uint16_t len)
    {
        if (len < sizeof(Msg_C2S_Chat)) return;
        const auto* req = reinterpret_cast<const Msg_C2S_Chat*>(data);
        Msg_S2C_Chat notify{};
        notify.channel = req->channel;
        strncpy(notify.content, req->content, sizeof(notify.content));
        BroadcastToMap(0, (uint16_t)ClientMsgID::S2C_CHAT_NOTIFY,
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

    /** @brief 广播消息给指定地图内的所有玩家 */
    void BroadcastToMap(uint32_t mapID, uint16_t msgID, const char* data, uint16_t len)
    {
        (void)mapID;
        for (auto& [rid, role] : m_roles)
            SendToClient(role->gatewayClientConn, msgID, data, len);
    }

    /** @brief Lua 回调：角色进入场景 */
    void CallLuaOnEnter(RoleID roleID, uint32_t mapID)
    {
        if (!m_lua) return;
        lua_getglobal(m_lua, "OnRoleEnter");
        if (lua_isfunction(m_lua, -1))
        {
            lua_pushinteger(m_lua, (lua_Integer)roleID);
            lua_pushinteger(m_lua, (lua_Integer)mapID);
            lua_pcall(m_lua, 2, 0, 0);
        }
        else lua_pop(m_lua, 1);
    }

    /** @brief Lua 回调：角色离开场景 */
    void CallLuaOnLeave(RoleID roleID)
    {
        if (!m_lua) return;
        lua_getglobal(m_lua, "OnRoleLeave");
        if (lua_isfunction(m_lua, -1))
        {
            lua_pushinteger(m_lua, (lua_Integer)roleID);
            lua_pcall(m_lua, 1, 0, 0);
        }
        else lua_pop(m_lua, 1);
    }

    /** @brief Lua 回调：通用消息处理（OnMsg_XXXX） */
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

    /** @brief 每帧 Tick（驱动角色 OnTick + Lua OnTick） */
    void OnTick()
    {
        uint64_t now = TimerMgr::NowMs();
        for (auto& [rid, role] : m_roles) role->OnTick(now);
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
        reg.port       = 9004;
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

    /** @brief 地图实例：mapID → MapInstance */
    std::unordered_map<uint32_t, MapInstance>             m_maps;
    /** @brief 在线角色：roleID → SceneRole */
    std::unordered_map<RoleID, std::shared_ptr<SceneRole>> m_roles;
};
