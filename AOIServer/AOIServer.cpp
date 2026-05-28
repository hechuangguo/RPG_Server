/**
 * @file AOIServer.cpp
 * @brief AOIServer 非内联方法实现。
 */

#include "AOIServer.h"

#include <cstring>

AOIServer::AOIServer()
    : m_server(this), m_superClient(this), m_sessionClient(this)
{
}

bool AOIServer::Init(const std::string& ip, uint16_t port,
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

void AOIServer::Run()
{
    while (true)
    {
        m_superClient.Poll(0);
        m_sessionClient.Poll(0);
        m_server.Poll(10);
        TimerMgr::Instance().Update();
    }
}

void AOIServer::OnConnect(ConnID id)
{
    LOG_DEBUG("AOI InnerConn=%u", id);
}

void AOIServer::OnDisconnect(ConnID id)
{
    LOG_WARN("AOI InnerConn=%u lost", id);
}

void AOIServer::OnMessage(ConnID id, uint8_t module, uint8_t sub,
                          const char* data, uint16_t len)
{
    MsgDispatcher::Instance().Dispatch(id, module, sub, data, len);
}

void AOIServer::RegisterHandlers()
{
    auto& d = MsgDispatcher::Instance();
    d.Register((uint16_t)InternalMsgID::AOI_ENTER_REQ,
        [this](uint32_t c, const char* d, uint16_t l){ OnEnter(c, d, l); });
    d.Register((uint16_t)InternalMsgID::AOI_LEAVE_REQ,
        [this](uint32_t c, const char* d, uint16_t l){ OnLeave(c, d, l); });
    d.Register((uint16_t)InternalMsgID::AOI_MOVE_REQ,
        [this](uint32_t c, const char* d, uint16_t l){ OnMove(c, d, l); });
    d.Register((uint16_t)InternalMsgID::AOI_SCENE_REGISTER,
        [this](uint32_t c, const char* d, uint16_t l){ OnSceneRegister(c, d, l); });
    d.Register((uint16_t)InternalMsgID::AOI_SCENE_UNREGISTER,
        [this](uint32_t c, const char* d, uint16_t l){ OnSceneUnregister(c, d, l); });
}

Grid AOIServer::WorldToGrid(float x, float z)
{
    return { (int)floorf(x / GRID_SIZE), (int)floorf(z / GRID_SIZE) };
}

std::vector<Grid> AOIServer::GetNeighborGrids(const Grid& g)
{
    std::vector<Grid> grids;
    grids.reserve(9);
    for (int dz = -1; dz <= 1; ++dz)
        for (int dx = -1; dx <= 1; ++dx)
            grids.push_back({ g.gx + dx, g.gz + dz });
    return grids;
}

std::vector<uint64_t> AOIServer::GetEntitiesInGrid(uint32_t mapID, const Grid& g)
{
    uint64_t key = ((uint64_t)mapID << 32) | (uint64_t)(((uint32_t)g.gx << 16) | (uint16_t)g.gz);
    auto it = m_gridMap.find(key);
    if (it == m_gridMap.end()) return {};
    return std::vector<uint64_t>(it->second.begin(), it->second.end());
}

void AOIServer::OnEnter(ConnID fromConn, const char* data, uint16_t len)
{
    if (len < sizeof(Msg_AOI_Move)) return;
    const auto* req = reinterpret_cast<const Msg_AOI_Move*>(data);
    AOIEntity e;
    e.entityID    = req->entityID;
    e.mapID       = req->mapID;
    e.x           = req->x; e.y = req->y; e.z = req->z;
    e.sceneConnID = fromConn;
    e.isPlayer    = (req->entityType == 0);
    m_entities[e.entityID] = e;

    Grid g = WorldToGrid(e.x, e.z);
    uint64_t key = ((uint64_t)e.mapID << 32) | (uint32_t)(((uint16_t)g.gx << 16) | (uint16_t)g.gz);
    m_gridMap[key].insert(e.entityID);
    m_entityGrid[e.entityID] = g;

    NotifyViewChange(e.entityID, true);
    LOG_DEBUG("AOI Enter: entityID=%llu map=%u (%.1f,%.1f)", e.entityID, e.mapID, e.x, e.z);
}

void AOIServer::OnLeave(ConnID /*fromConn*/, const char* data, uint16_t len)
{
    if (len < sizeof(uint64_t)) return;
    uint64_t eid = *reinterpret_cast<const uint64_t*>(data);
    NotifyViewChange(eid, false);
    auto it = m_entities.find(eid);
    if (it != m_entities.end())
    {
        auto& e = it->second;
        Grid g  = m_entityGrid[eid];
        uint64_t key = ((uint64_t)e.mapID << 32) | (uint32_t)(((uint16_t)g.gx << 16) | (uint16_t)g.gz);
        m_gridMap[key].erase(eid);
        m_entityGrid.erase(eid);
        m_entities.erase(it);
    }
}

void AOIServer::OnMove(ConnID fromConn, const char* data, uint16_t len)
{
    if (len < sizeof(Msg_AOI_Move)) return;
    const auto* req = reinterpret_cast<const Msg_AOI_Move*>(data);
    auto it = m_entities.find(req->entityID);
    if (it == m_entities.end()) return;
    auto& e = it->second;
    Grid oldG = m_entityGrid[e.entityID];
    Grid newG = WorldToGrid(req->x, req->z);

    if (oldG.gx != newG.gx || oldG.gz != newG.gz)
    {
        uint64_t oldKey = ((uint64_t)e.mapID << 32) | (uint32_t)(((uint16_t)oldG.gx << 16) | (uint16_t)oldG.gz);
        uint64_t newKey = ((uint64_t)e.mapID << 32) | (uint32_t)(((uint16_t)newG.gx << 16) | (uint16_t)newG.gz);
        m_gridMap[oldKey].erase(e.entityID);
        m_gridMap[newKey].insert(e.entityID);
        m_entityGrid[e.entityID] = newG;
    }
    e.x = req->x; e.y = req->y; e.z = req->z;

    m_server.SendMsg(fromConn, (uint16_t)InternalMsgID::AOI_VIEW_NOTIFY, data, len);
}

void AOIServer::OnSceneRegister(ConnID /*fromConn*/, const char* data, uint16_t len)
{
    if (len < sizeof(Msg_AOI_SceneRegister)) return;
    const auto* req = reinterpret_cast<const Msg_AOI_SceneRegister*>(data);
    m_scenes[req->sceneInstanceId] = *req;
    LOG_INFO("AOI scene register: instance=%llu map=%u server=%u kind=%u",
             req->sceneInstanceId, req->mapId, req->sceneServerId, req->sceneKind);
}

void AOIServer::OnSceneUnregister(ConnID /*fromConn*/, const char* data, uint16_t len)
{
    if (len < sizeof(Msg_AOI_SceneUnregister)) return;
    const auto* req = reinterpret_cast<const Msg_AOI_SceneUnregister*>(data);
    m_scenes.erase(req->sceneInstanceId);
    LOG_INFO("AOI scene unregister: instance=%llu", req->sceneInstanceId);
}

void AOIServer::NotifyViewChange(uint64_t entityID, bool enter)
{
    auto it = m_entities.find(entityID);
    if (it == m_entities.end()) return;
    char buf[sizeof(uint64_t) + 1];
    memcpy(buf, &entityID, sizeof(uint64_t));
    buf[sizeof(uint64_t)] = enter ? 1 : 0;
    m_server.SendMsg(it->second.sceneConnID,
                     (uint16_t)InternalMsgID::AOI_VIEW_NOTIFY, buf, sizeof(buf));
}

void AOIServer::RegisterToSuper()
{
    Msg_S2S_Register reg{};
    reg.serverType = (uint8_t)SubServerType::AOI;
    reg.serverID   = 1;
    copyToWire(reg.ip, sizeof(reg.ip), "127.0.0.1");
    reg.port       = 9003;
    m_superClient.SendMsg((uint16_t)InternalMsgID::S2S_REGISTER_REQ,
                          reinterpret_cast<char*>(&reg), sizeof(reg));
}

void AOIServer::SendHeartbeat()
{
    Msg_S2S_Heartbeat hb{}; hb.seq = ++m_hbSeq; hb.timestamp = TimerMgr::NowMs();
    m_superClient.SendMsg((uint16_t)InternalMsgID::S2S_HEARTBEAT,
                          reinterpret_cast<char*>(&hb), sizeof(hb));
}
