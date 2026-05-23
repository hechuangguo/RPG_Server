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

// Lua 头文件
extern "C"
{
    #include "lua.h"
    #include "lualib.h"
    #include "lauxlib.h"
}

// ============================================================
//  SceneServer —— 处理角色在线数据、地图逻辑、Lua 脚本
//  依赖 SessionServer、RecordServer、GatewayServer、AOIServer
// ============================================================

// SceneServer 角色（在线完整数据）
class SceneRole : public IRole
{
public:
    explicit SceneRole(const RoleBase& base) : IRole(base) {}
    ConnID   gatewayConnID     = INVALID_CONN_ID;
    uint32_t gatewayClientConn = 0;  // 在 GatewayServer 的客户端连接 ID
};

// 地图实例
struct MapInstance
{
    uint32_t                   mapID;
    std::string                mapName;
    uint32_t                   maxPlayer;
    std::vector<RoleID>        players;
};

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

    bool Init(const std::string& ip, uint16_t port,
              const ServerConfig& cfg, const SceneServerInfo& sceneInfo)
    {
        Logger::Instance().SetServerName("SceneServer");
        m_sceneID = sceneInfo.sceneID;
        if (!m_server.Start(ip, port)) { LOG_FATAL("SceneServer start failed"); return false; }

        // 连接各依赖服务器
        m_superClient.Connect(cfg.superIP,       (uint16_t)cfg.superPort);
        m_sessionClient.Connect("127.0.0.1",     (uint16_t)cfg.sessionPort);
        m_recordClient.Connect("127.0.0.1",      (uint16_t)cfg.recordPort);
        m_aoiClient.Connect("127.0.0.1",         (uint16_t)cfg.aoiPort);
        m_gatewayClient.Connect("127.0.0.1",     (uint16_t)cfg.gatewayPort);

        // 加载地图配置
        for (auto& mc : sceneInfo.maps)
        {
            MapInstance mi;
            mi.mapID     = mc.mapID;
            mi.mapName   = mc.mapName;
            mi.maxPlayer = mc.maxPlayer;
            m_maps[mc.mapID] = mi;
            LOG_INFO("Map loaded: id=%u name=%s", mc.mapID, mc.mapName.c_str());
        }

        // 初始化 Lua
        InitLua();
        RegisterHandlers();

        TimerMgr::Instance().Register(500,   0,    [this]{ RegisterToSuper(); });
        TimerMgr::Instance().Register(10000, 10000,[this]{ SendHeartbeat(); });
        TimerMgr::Instance().Register(1000,  1000, [this]{ OnTick(); });

        LOG_INFO("SceneServer %u started on %s:%d", m_sceneID, ip.c_str(), port);
        return true;
    }

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
    void InitLua()
    {
        m_lua = luaL_newstate();
        luaL_openlibs(m_lua);
        // 注册 C++ 函数到 Lua
        lua_register(m_lua, "log_info",     LuaLogInfo);
        lua_register(m_lua, "send_to_role", LuaSendToRole);
        // 加载脚本目录
        luaL_dostring(m_lua, "package.path = package.path .. ';../script/?.lua'");
        if (luaL_dofile(m_lua, "../script/scene/init.lua") != LUA_OK)
            LOG_WARN("Lua init.lua load failed: %s", lua_tostring(m_lua, -1));
    }

    static int LuaLogInfo(lua_State* L)
    {
        const char* msg = luaL_checkstring(L, 1);
        LOG_INFO("[Lua] %s", msg);
        return 0;
    }
    static int LuaSendToRole(lua_State* L)
    {
        // 供 Lua 脚本调用：sendToRole(roleID, msgID, data)
        (void)L;
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

    void OnRoleEnter(ConnID fromConn, const char* data, uint16_t len)
    {
        if (len < sizeof(Msg_SCE_RoleEnterReq)) return;
        const auto* req = reinterpret_cast<const Msg_SCE_RoleEnterReq*>(data);
        LOG_INFO("RoleEnter: roleID=%llu mapID=%u", req->roleID, req->mapID);

        auto it = m_maps.find(req->mapID);
        if (it == m_maps.end())
        {
            LOG_WARN("Map %u not found on SceneServer %u", req->mapID, m_sceneID);
            return;
        }
        // 创建在线角色
        RoleBase base; base.roleID = req->roleID;
        base.mapID = req->mapID; base.posX = req->x; base.posZ = req->z;
        auto role = std::make_shared<SceneRole>(base);
        m_roles[req->roleID] = role;
        it->second.players.push_back(req->roleID);

        // 通知 AOI
        Msg_AOI_Move aoi{};
        aoi.entityID = req->roleID;
        aoi.mapID    = req->mapID;
        aoi.x = req->x; aoi.z = req->z;
        m_aoiClient.SendMsg((uint16_t)InternalMsgID::AOI_ENTER_REQ,
                             reinterpret_cast<char*>(&aoi), sizeof(aoi));

        // 回包给 GatewayServer
        Msg_SCE_RoleEnterReq rsp = *req;
        m_server.SendMsg(fromConn, (uint16_t)InternalMsgID::SCE_ROLE_ENTER_RSP,
                         reinterpret_cast<char*>(&rsp), sizeof(rsp));

        // 调用 Lua 脚本
        CallLuaOnEnter(req->roleID, req->mapID);
    }

    void OnRoleLeave(ConnID fromConn, const char* data, uint16_t len)
    {
        if (len < sizeof(RoleID)) return;
        RoleID rid = *reinterpret_cast<const RoleID*>(data);
        auto it = m_roles.find(rid);
        if (it == m_roles.end()) return;

        // 通知 AOI 离开
        m_aoiClient.SendMsg((uint16_t)InternalMsgID::AOI_LEAVE_REQ,
                             reinterpret_cast<const char*>(&rid), sizeof(rid));

        // 保存数据到 RecordServer
        m_recordClient.SendMsg((uint16_t)InternalMsgID::REC_SAVE_ROLE_REQ,
                                reinterpret_cast<const char*>(&rid), sizeof(rid));

        // 调用 Lua
        CallLuaOnLeave(rid);
        m_roles.erase(it);
        LOG_INFO("RoleLeave: roleID=%llu", rid);
    }

    void OnClientMsg(ConnID fromConn, const char* data, uint16_t len)
    {
        // GatewayServer 转发来的客户端消息
        if (len < sizeof(Msg_GW_ClientMsg)) return;
        const auto* hdr = reinterpret_cast<const Msg_GW_ClientMsg*>(data);
        const char* body = data + sizeof(Msg_GW_ClientMsg);
        uint16_t bodyLen = hdr->dataLen;
        LOG_DEBUG("ClientMsg: connID=%u msgID=0x%04X", hdr->clientConnID, hdr->msgID);
        // 根据 msgID 分发到具体处理函数（或 Lua）
        HandleClientMsg(hdr->clientConnID, hdr->msgID, body, bodyLen);
    }

    void OnViewNotify(ConnID fromConn, const char* data, uint16_t len)
    {
        // AOI 通知视野变化，广播给相关玩家
        LOG_DEBUG("ViewNotify len=%d", len);
    }

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
        default:
            // 尝试 Lua 处理
            CallLuaMsgHandler(clientConnID, msgID, data, len);
        }
    }

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

        // 更新 AOI
        Msg_AOI_Move aoi{};
        aoi.entityID = req->roleID;
        aoi.mapID    = role->Base().mapID;
        aoi.x = req->x; aoi.y = req->y; aoi.z = req->z; aoi.dir = req->dir;
        m_aoiClient.SendMsg((uint16_t)InternalMsgID::AOI_MOVE_REQ,
                             reinterpret_cast<char*>(&aoi), sizeof(aoi));
    }

    void OnChatReq(uint32_t clientConnID, const char* data, uint16_t len)
    {
        if (len < sizeof(Msg_C2S_Chat)) return;
        const auto* req = reinterpret_cast<const Msg_C2S_Chat*>(data);
        // 世界聊天 → 广播给所有在线玩家（通过 GatewayServer）
        Msg_S2C_Chat notify{};
        notify.channel = req->channel;
        strncpy(notify.content, req->content, sizeof(notify.content));
        // TODO: 填充 fromID/fromName
        BroadcastToMap(0, (uint16_t)ClientMsgID::S2C_CHAT_NOTIFY,
                       reinterpret_cast<char*>(&notify), sizeof(notify));
    }

    void OnSkillReq(uint32_t clientConnID, const char* data, uint16_t len)
    {
        LOG_DEBUG("SkillReq from conn=%u", clientConnID);
        // 委托 Lua 处理技能逻辑
        CallLuaSkillHandler(clientConnID, data, len);
    }

    void OnHeartbeatReq(uint32_t clientConnID, const char* data, uint16_t len)
    {
        Msg_S2C_Heartbeat rsp{};
        if (len >= sizeof(Msg_C2S_Heartbeat))
            rsp.seq = reinterpret_cast<const Msg_C2S_Heartbeat*>(data)->seq;
        rsp.serverTime = TimerMgr::NowMs();
        // 通过 GatewayServer 发回客户端
        SendToClient(clientConnID, (uint16_t)ClientMsgID::S2C_HEARTBEAT,
                     reinterpret_cast<char*>(&rsp), sizeof(rsp));
    }

    // 向客户端发送消息（通过 GatewayServer 转发）
    void SendToClient(uint32_t clientConnID, uint16_t msgID, const char* data, uint16_t len)
    {
        // 包结构：[clientConnID(4)][msgID(2)][data]
        std::vector<char> buf(sizeof(uint32_t) + sizeof(uint16_t) + len);
        memcpy(buf.data(), &clientConnID, sizeof(uint32_t));
        memcpy(buf.data() + sizeof(uint32_t), &msgID, sizeof(uint16_t));
        if (len > 0) memcpy(buf.data() + sizeof(uint32_t) + sizeof(uint16_t), data, len);
        m_gatewayClient.SendMsg((uint16_t)InternalMsgID::GW_SEND_TO_CLIENT,
                                 buf.data(), (uint16_t)buf.size());
    }

    // 广播给地图内所有玩家
    void BroadcastToMap(uint32_t mapID, uint16_t msgID, const char* data, uint16_t len)
    {
        (void)mapID;
        for (auto& [rid, role] : m_roles)
            SendToClient(role->gatewayClientConn, msgID, data, len);
    }

    // Lua 回调
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

    void OnTick()
    {
        uint64_t now = TimerMgr::NowMs();
        for (auto& [rid, role] : m_roles) role->OnTick(now);
        // Lua tick
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

    TcpServer  m_server;
    TcpClient  m_superClient;
    TcpClient  m_sessionClient;
    TcpClient  m_recordClient;
    TcpClient  m_aoiClient;
    TcpClient  m_gatewayClient;
    TcpClient  m_globalClient;
    TcpClient  m_zoneClient;
    lua_State* m_lua;
    uint32_t   m_sceneID;
    uint32_t   m_hbSeq = 0;
    std::unordered_map<uint32_t, MapInstance>             m_maps;
    std::unordered_map<RoleID, std::shared_ptr<SceneRole>> m_roles;
};
