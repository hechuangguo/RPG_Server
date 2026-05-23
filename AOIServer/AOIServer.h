#pragma once
#include "../sdk/net/TcpServer.h"
#include "../sdk/net/TcpClient.h"
#include "../sdk/util/RoleBase.h"
#include "../sdk/util/MsgDispatcher.h"
#include "../sdk/log/Logger.h"
#include "../sdk/timer/TimerMgr.h"
#include "../protocal/InternalMsg.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cmath>

// ============================================================
//  AOIServer —— 角色视野 / 9宫格管理
//  依赖 SessionServer
// ============================================================

// 9宫格格子大小（单位：游戏坐标）
constexpr float GRID_SIZE = 200.f;

struct AOIEntity
{
    uint64_t entityID;
    uint32_t mapID;
    float    x, y, z;
    bool     isPlayer;
    ConnID   sceneConnID;  // 所在 SceneServer 连接 ID
};

struct Grid
{
    int gx, gz;
    bool operator==(const Grid& o) const { return gx==o.gx && gz==o.gz; }
};
struct GridHash
{
    size_t operator()(const Grid& g) const
    { return std::hash<int64_t>()((int64_t)g.gx << 32 | (uint32_t)g.gz); }
};

class AOIServer : public INetCallback
{
public:
    AOIServer() : m_server(this), m_superClient(this), m_sessionClient(this) {}

    bool Init(const std::string& ip, uint16_t port,
              const std::string& superIP, uint16_t superPort,
              const std::string& sessionIP, uint16_t sessionPort)
    {
        Logger::Instance().SetServerName("AOIServer");
        if (!m_server.Start(ip, port)) { LOG_FATAL("AOIServer start failed"); return false; }
        m_superClient.Connect(superIP, superPort);
        m_sessionClient.Connect(sessionIP, sessionPort);
        RegisterHandlers();
        TimerMgr::Instance().Register(500,   0,     [this]{ RegisterToSuper(); });
        TimerMgr::Instance().Register(10000, 10000, [this]{ SendHeartbeat(); });
        LOG_INFO("AOIServer started on %s:%d", ip.c_str(), port);
        return true;
    }

    void Run()
    {
        while(true)
        {
            m_superClient.Poll(0);
            m_sessionClient.Poll(0);
            m_server.Poll(10);
            TimerMgr::Instance().Update();
        }
    }

    void OnConnect(ConnID id)    override { LOG_DEBUG("AOI InnerConn=%u", id); }
    void OnDisconnect(ConnID id) override { LOG_WARN("AOI InnerConn=%u lost", id); }
    void OnMessage(ConnID id, uint16_t msgID, const char* data, uint16_t len) override
    {
        MsgDispatcher::Instance().Dispatch(id, msgID, data, len);
    }

private:
    void RegisterHandlers()
    {
        auto& d = MsgDispatcher::Instance();
        d.Register((uint16_t)InternalMsgID::AOI_ENTER_REQ,
            [this](uint32_t c, const char* d, uint16_t l){ OnEnter(c, d, l); });
        d.Register((uint16_t)InternalMsgID::AOI_LEAVE_REQ,
            [this](uint32_t c, const char* d, uint16_t l){ OnLeave(c, d, l); });
        d.Register((uint16_t)InternalMsgID::AOI_MOVE_REQ,
            [this](uint32_t c, const char* d, uint16_t l){ OnMove(c, d, l); });
    }

    Grid WorldToGrid(float x, float z)
    {
        return { (int)floorf(x / GRID_SIZE), (int)floorf(z / GRID_SIZE) };
    }

    // 获取 3x3 邻域格子
    std::vector<Grid> GetNeighborGrids(const Grid& g)
    {
        std::vector<Grid> grids;
        grids.reserve(9);
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx)
                grids.push_back({ g.gx + dx, g.gz + dz });
        return grids;
    }

    // 获取格子内所有实体
    std::vector<uint64_t> GetEntitiesInGrid(uint32_t mapID, const Grid& g)
    {
        uint64_t key = ((uint64_t)mapID << 32) | (uint64_t)(((uint32_t)g.gx << 16) | (uint16_t)g.gz);
        auto it = m_gridMap.find(key);
        if (it == m_gridMap.end()) return {};
        return std::vector<uint64_t>(it->second.begin(), it->second.end());
    }

    void OnEnter(ConnID fromConn, const char* data, uint16_t len)
    {
        if (len < sizeof(Msg_AOI_Move)) return;
        const auto* req = reinterpret_cast<const Msg_AOI_Move*>(data);
        AOIEntity e;
        e.entityID   = req->entityID;
        e.mapID      = req->mapID;
        e.x          = req->x; e.y = req->y; e.z = req->z;
        e.sceneConnID= fromConn;
        e.isPlayer   = true;
        m_entities[e.entityID] = e;

        Grid g = WorldToGrid(e.x, e.z);
        uint64_t key = ((uint64_t)e.mapID << 32) | (uint32_t)(((uint16_t)g.gx << 16) | (uint16_t)g.gz);
        m_gridMap[key].insert(e.entityID);
        m_entityGrid[e.entityID] = g;

        // 通知周围实体视野进入
        NotifyViewChange(e.entityID, true);
        LOG_DEBUG("AOI Enter: entityID=%llu map=%u (%.1f,%.1f)", e.entityID, e.mapID, e.x, e.z);
    }

    void OnLeave(ConnID fromConn, const char* data, uint16_t len)
    {
        if (len < sizeof(uint64_t)) return;
        uint64_t eid = *reinterpret_cast<const uint64_t*>(data);
        NotifyViewChange(eid, false);
        auto it = m_entities.find(eid);
        if (it != m_entities.end())
        {
            auto& e = it->second;
            Grid g  = m_entityGrid[eid];
            uint64_t key = ((uint64_t)e.mapID << 32) | (uint32_t)(((uint16_t)g.gx << 16)|(uint16_t)g.gz);
            m_gridMap[key].erase(eid);
            m_entityGrid.erase(eid);
            m_entities.erase(it);
        }
    }

    void OnMove(ConnID fromConn, const char* data, uint16_t len)
    {
        if (len < sizeof(Msg_AOI_Move)) return;
        const auto* req = reinterpret_cast<const Msg_AOI_Move*>(data);
        auto it = m_entities.find(req->entityID);
        if (it == m_entities.end()) return;
        auto& e  = it->second;
        Grid oldG = m_entityGrid[e.entityID];
        Grid newG = WorldToGrid(req->x, req->z);

        if (oldG.gx != newG.gx || oldG.gz != newG.gz)
        {
            // 更新格子
            uint64_t oldKey = ((uint64_t)e.mapID<<32)|(uint32_t)(((uint16_t)oldG.gx<<16)|(uint16_t)oldG.gz);
            uint64_t newKey = ((uint64_t)e.mapID<<32)|(uint32_t)(((uint16_t)newG.gx<<16)|(uint16_t)newG.gz);
            m_gridMap[oldKey].erase(e.entityID);
            m_gridMap[newKey].insert(e.entityID);
            m_entityGrid[e.entityID] = newG;
        }
        e.x = req->x; e.y = req->y; e.z = req->z;

        // 广播移动给视野内玩家（通过 SceneServer）
        m_server.SendMsg(fromConn, (uint16_t)InternalMsgID::AOI_VIEW_NOTIFY, data, len);
    }

    void NotifyViewChange(uint64_t entityID, bool enter)
    {
        auto it = m_entities.find(entityID);
        if (it == m_entities.end()) return;
        // 此处简化：直接通知 SceneServer 处理视野进入/离开
        char buf[sizeof(uint64_t) + 1];
        memcpy(buf, &entityID, sizeof(uint64_t));
        buf[sizeof(uint64_t)] = enter ? 1 : 0;
        m_server.SendMsg(it->second.sceneConnID,
                         (uint16_t)InternalMsgID::AOI_VIEW_NOTIFY, buf, sizeof(buf));
    }

    void RegisterToSuper()
    {
        Msg_S2S_Register reg{};
        reg.serverType = (uint8_t)SubServerType::AOI;
        reg.serverID   = 1;
        strncpy(reg.ip, "127.0.0.1", sizeof(reg.ip));
        reg.port       = 9003;
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
    uint32_t   m_hbSeq = 0;

    std::unordered_map<uint64_t, AOIEntity>                    m_entities;
    std::unordered_map<uint64_t, Grid>                         m_entityGrid;
    std::unordered_map<uint64_t, std::unordered_set<uint64_t>> m_gridMap;
};
