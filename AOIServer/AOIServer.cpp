/**
 * @file AOIServer.cpp
 * @brief AOIServer 非内联方法实现。
 */

#include "AOIServer.h"
#include "AoiInternMsgRegister.h"
#include "../sdk/net/MsgIngress.h"
#include "../sdk/net/NetTls.h"
#include "../sdk/util/ServerBootstrap.h"

#include <cstring>

AOIServer::AOIServer()
    : m_server(this)
    , m_superClient(this)
    , m_externSender(m_superClient, SubServerType::AOI, 0)
{
}

bool AOIServer::Init(const std::string& ip, uint16_t port,
                     const ServerConfig& cfg, const ServerList& list, uint32_t selfId)
{
    Logger::Instance().SetServerName("AOIServer");
    m_defaultGridSize = cfg.aoiGridSize > 0.f ? cfg.aoiGridSize : GRID_SIZE;
    if (const ServerEntry* self = list.find(SubServerType::AOI, selfId))
        m_self = *self;
    m_externSender.setSelfId(m_self.id ? m_self.id : selfId);
    ServerBootstrap::bindRemoteLog(m_externSender, SubServerType::AOI);
    wireTlsServer(m_server);
    if (!m_server.Start(ip, port)) { LOG_FATAL("视野服启动失败"); return false; }
    wireTlsClient(m_superClient);
    m_superClient.Connect(cfg.superIP, (uint16_t)cfg.superPort);
    registerHandlers();
    TimerMgr::Instance().Register(500,   0,     [this]{ RegisterToSuper(); });
    TimerMgr::Instance().Register(10000, 10000, [this]{ sendHeartbeat(); });
    LOG_INFO("视野服启动完成: %s:%d", ip.c_str(), port);
    return true;
}

void AOIServer::Run()
{
    while (true)
    {
        m_server.Poll(10);
        TimerMgr::Instance().Update();
        m_superClient.Poll(0);
    }
}

void AOIServer::OnConnect(ConnID id)
{
    LOG_DEBUG("视野服内部连接建立: conn=%u", id);
}

void AOIServer::OnDisconnect(ConnID id)
{
    LOG_WARN("视野服内部连接断开: conn=%u", id);
}

void AOIServer::OnMessage(ConnID id, uint8_t module, uint8_t sub,
                          const char* data, uint16_t len)
{
    MsgIngress::dispatchInternal(id, module, sub, data, len);
}

void AOIServer::registerHandlers()
{
    AoiInternMsgRegister(*this);
}

Grid AOIServer::WorldToGrid(uint32_t mapId, float x, float z)
{
    const float gs = gridSizeForMap(mapId);
    return { static_cast<int>(floorf(x / gs)), static_cast<int>(floorf(z / gs)) };
}

float AOIServer::gridSizeForMap(uint32_t mapId) const
{
    auto it = m_mapGridSize.find(mapId);
    if (it != m_mapGridSize.end() && it->second > 0.f)
        return it->second;
    return m_defaultGridSize;
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

void AOIServer::onEnter(ConnID fromConn, const char* data, uint16_t len)
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

    Grid g = WorldToGrid(e.mapID, e.x, e.z);
    uint64_t key = ((uint64_t)e.mapID << 32) | (uint32_t)(((uint16_t)g.gx << 16) | (uint16_t)g.gz);
    m_gridMap[key].insert(e.entityID);
    m_entityGrid[e.entityID] = g;

    notifyViewChange(e.entityID, true);
    LOG_DEBUG("视野进入: entityID=%llu map=%u (%.1f,%.1f)", e.entityID, e.mapID, e.x, e.z);
}

void AOIServer::onLeave(ConnID /*fromConn*/, const char* data, uint16_t len)
{
    if (len < sizeof(uint64_t)) return;
    uint64_t eid = *reinterpret_cast<const uint64_t*>(data);
    notifyViewChange(eid, false);
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

void AOIServer::onMove(ConnID fromConn, const char* data, uint16_t len)
{
    if (len < sizeof(Msg_AOI_Move)) return;
    const auto* req = reinterpret_cast<const Msg_AOI_Move*>(data);
    auto it = m_entities.find(req->entityID);
    if (it == m_entities.end()) return;
    auto& e = it->second;
    Grid oldG = m_entityGrid[e.entityID];
    Grid newG = WorldToGrid(e.mapID, req->x, req->z);

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

void AOIServer::onSceneRegister(ConnID /*fromConn*/, const char* data, uint16_t len)
{
    if (len < sizeof(Msg_AOI_SceneRegister)) return;
    const auto* req = reinterpret_cast<const Msg_AOI_SceneRegister*>(data);
    m_scenes[req->sceneInstanceId] = *req;
    if (req->aoiGridSize > 0.f)
        m_mapGridSize[req->mapId] = req->aoiGridSize;
    LOG_INFO("视野场景注册: instance=%llu map=%u server=%u kind=%u grid=%.0f",
             req->sceneInstanceId, req->mapId, req->sceneServerId, req->sceneKind,
             gridSizeForMap(req->mapId));
}

void AOIServer::onSceneUnregister(ConnID /*fromConn*/, const char* data, uint16_t len)
{
    if (len < sizeof(Msg_AOI_SceneUnregister)) return;
    const auto* req = reinterpret_cast<const Msg_AOI_SceneUnregister*>(data);
    m_scenes.erase(req->sceneInstanceId);
    LOG_INFO("视野场景注销: instance=%llu", req->sceneInstanceId);
}

void AOIServer::notifyViewChange(uint64_t entityID, bool enter)
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
    reg.serverID   = m_self.id;
    copyToWire(reg.ip, sizeof(reg.ip), m_self.ip.c_str());
    reg.port       = m_self.port;
    copyToWire(reg.name, sizeof(reg.name), m_self.name.c_str());
    m_superClient.SendMsg((uint16_t)InternalMsgID::S2S_REGISTER_REQ,
                          reinterpret_cast<char*>(&reg), sizeof(reg));
}

void AOIServer::sendHeartbeat()
{
    if (!m_superClient.canSend())
        return;
    Msg_S2S_Heartbeat hb{}; hb.seq = ++m_hbSeq; hb.timestamp = TimerMgr::NowMs();
    m_superClient.SendMsg((uint16_t)InternalMsgID::S2S_HEARTBEAT,
                          reinterpret_cast<char*>(&hb), sizeof(hb));
}
