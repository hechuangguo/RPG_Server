/**
 * @file    SuperServer.cpp
 * @brief   SuperServer 业务实现与消息处理逻辑
 */

#include "SuperServer.h"
#include "../sdk/util/ServerBootstrap.h"

#include <cstring>
#include <vector>

SuperServer::SuperServer()
    : m_server(this)
{
}

SuperServer::~SuperServer()
{
    if (m_db)
    {
        mysql_close(m_db);
        m_db = nullptr;
    }
}

bool SuperServer::Init(const std::string& ip, uint16_t port, const ServerConfig& cfg)
{
    Logger::Instance().SetServerName("SuperServer");
    LOG_INFO("SuperServer starting on %s:%d", ip.c_str(), port);

    if (!loadServerList(cfg))
    {
        LOG_FATAL("Load ServerList failed");
        return false;
    }

    if (!m_server.Start(ip, port))
    {
        LOG_FATAL("Start failed");
        return false;
    }

    RegisterHandlers();

    TimerMgr::Instance().Register(30000, 30000, [this] { CheckHeartbeat(); });
    LOG_INFO("SuperServer started.");
    return true;
}

bool SuperServer::loadServerList(const ServerConfig& cfg)
{
    m_db = mysql_init(nullptr);
    if (!m_db)
    {
        return false;
    }
    if (!mysql_real_connect(m_db, cfg.dbHost.c_str(), cfg.dbUser.c_str(), cfg.dbPass.c_str(),
                            cfg.dbName.c_str(), (unsigned int)cfg.dbPort, nullptr, 0))
    {
        LOG_ERR("MySQL connect failed: %s", mysql_error(m_db));
        return false;
    }
    mysql_set_character_set(m_db, "utf8mb4");

    const char* sql = "SELECT server_id, server_type, ip, port, name FROM ServerList";
    if (mysql_query(m_db, sql) != 0)
    {
        LOG_ERR("ServerList query failed: %s", mysql_error(m_db));
        return false;
    }
    MYSQL_RES* res = mysql_store_result(m_db);
    if (!res)
    {
        LOG_ERR("ServerList store_result failed: %s", mysql_error(m_db));
        return false;
    }
    m_serverList.clear();
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)) != nullptr)
    {
        ServerEntry e;
        e.id   = row[0] ? (uint32_t)strtoul(row[0], nullptr, 10) : 0;
        e.type = (SubServerType)(row[1] ? (uint8_t)strtoul(row[1], nullptr, 10) : 0);
        e.ip   = row[2] ? row[2] : "";
        e.port = row[3] ? (uint16_t)strtoul(row[3], nullptr, 10) : 0;
        e.name = row[4] ? row[4] : "";
        m_serverList.add(e);
    }
    mysql_free_result(res);
    LOG_INFO("ServerList loaded: %zu entries", m_serverList.size());
    return true;
}

void SuperServer::Run()
{
    while (true)
    {
        m_server.Poll(10);
        ServerBootstrap::tickGameZoneExtern(m_externHub);
        TimerMgr::Instance().Update();
    }
}

void SuperServer::setupExternalClients(const LoginServerList& list)
{
    ServerBootstrap::initGameZoneExtern(m_externHub, list, SubServerType::UNKNOWN, false, false);
}

void SuperServer::OnConnect(ConnID id)
{
    LOG_INFO("SubServer connected, connID=%u", id);
}

void SuperServer::OnDisconnect(ConnID id)
{
    LOG_WARN("SubServer disconnected, connID=%u", id);
    RemoveSubServer(id);
}

void SuperServer::OnMessage(ConnID id, uint8_t module, uint8_t sub, const char* data, uint16_t len)
{
    MsgDispatcher::Instance().Dispatch(id, module, sub, data, len);
}

void SuperServer::RegisterHandlers()
{
    auto& d = MsgDispatcher::Instance();
    d.Register((uint16_t)InternalMsgID::S2S_REGISTER_REQ,
               [this](uint32_t c, const char* d, uint16_t l) { OnRegister(c, d, l); });
    d.Register((uint16_t)InternalMsgID::S2S_HEARTBEAT,
               [this](uint32_t c, const char* d, uint16_t l) { OnHeartbeat(c, d, l); });
    d.Register((uint16_t)InternalMsgID::S2S_SERVERLIST_REQ,
               [this](uint32_t c, const char* d, uint16_t l) { OnServerListReq(c, d, l); });
    d.Register((uint16_t)InternalMsgID::GW_USER_LOGIN_REQ,
               [this](uint32_t c, const char* d, uint16_t l) { OnUserLoginReq(c, d, l); });
    d.Register((uint16_t)InternalMsgID::REC_LOAD_USER_RSP,
               [this](uint32_t c, const char* d, uint16_t l) { OnLoadUserRsp(c, d, l); });
    d.Register((uint16_t)InternalMsgID::SCE_USER_ENTER_RSP,
               [this](uint32_t c, const char* d, uint16_t l) { OnUserEnterRsp(c, d, l); });
    d.Register((uint16_t)InternalMsgID::SS_KICK_USER,
               [this](uint32_t c, const char* d, uint16_t l) { OnKickUser(c, d, l); });
}

void SuperServer::OnRegister(ConnID connID, const char* data, uint16_t len)
{
    if (len < sizeof(Msg_S2S_Register))
    {
        return;
    }

    const auto* req = reinterpret_cast<const Msg_S2S_Register*>(data);
    SubServerInfo info;
    info.connID = connID;
    info.type = (SubServerType)req->serverType;
    info.serverID = req->serverID;
    info.ip = req->ip;
    info.port = req->port;
    info.name = req->name;
    info.alive = true;
    info.lastHeartbeat = TimerMgr::NowMs();
    m_servers[connID] = info;
    LOG_INFO("SubServer registered: type=%d serverID=%u ip=%s port=%d name=%s",
             (int)info.type, info.serverID, info.ip.c_str(), info.port, info.name.c_str());

    char rsp[4] = {0};
    m_server.SendMsg(connID, (uint16_t)InternalMsgID::S2S_REGISTER_RSP, rsp, sizeof(rsp));
}

void SuperServer::OnHeartbeat(ConnID connID, const char* data, uint16_t len)
{
    auto it = m_servers.find(connID);
    if (it != m_servers.end())
    {
        it->second.lastHeartbeat = TimerMgr::NowMs();
    }

    Msg_S2S_Heartbeat ack{};
    if (len >= sizeof(Msg_S2S_Heartbeat))
    {
        ack.seq = reinterpret_cast<const Msg_S2S_Heartbeat*>(data)->seq;
    }
    ack.timestamp = TimerMgr::NowMs();
    m_server.SendMsg(connID, (uint16_t)InternalMsgID::S2S_HEARTBEAT_ACK,
                     reinterpret_cast<char*>(&ack), sizeof(ack));
}

void SuperServer::OnServerListReq(ConnID connID, const char* /*data*/, uint16_t /*len*/)
{
    const auto& entries = m_serverList.all();
    uint16_t count = (uint16_t)entries.size();

    std::vector<char> buf(sizeof(Msg_S2S_ServerListRsp) + (size_t)count * sizeof(Msg_ServerEntry));
    auto* hdr = reinterpret_cast<Msg_S2S_ServerListRsp*>(buf.data());
    hdr->count = count;

    char* p = buf.data() + sizeof(Msg_S2S_ServerListRsp);
    for (const auto& e : entries)
    {
        Msg_ServerEntry wire{};
        wire.serverID   = e.id;
        wire.serverType = (uint8_t)e.type;
        snprintf(wire.ip, sizeof(wire.ip), "%s", e.ip.c_str());
        wire.port = e.port;
        snprintf(wire.name, sizeof(wire.name), "%s", e.name.c_str());
        memcpy(p, &wire, sizeof(wire));
        p += sizeof(wire);
    }

    m_server.SendMsg(connID, (uint16_t)InternalMsgID::S2S_SERVERLIST_RSP,
                     buf.data(), (uint16_t)buf.size());
    LOG_INFO("ServerList sent to connID=%u (%u entries)", connID, count);
}

void SuperServer::OnUserLoginReq(ConnID connID, const char* data, uint16_t len)
{
    if (len < sizeof(Msg_REC_LoginVerifyRsp))
    {
        return;
    }
    const auto* rsp = reinterpret_cast<const Msg_REC_LoginVerifyRsp*>(data);
    if (rsp->code != 0)
    {
        return;
    }

    ConnID sceneConn = FindSceneServer();
    if (sceneConn == INVALID_CONN_ID)
    {
        LOG_WARN("UserLogin: no SceneServer available userID=%llu", rsp->userID);
        SendLoginFailToGateway(connID, rsp->gatewayConnID, -1);
        return;
    }

    PendingLogin pending{};
    pending.userID = rsp->userID;
    pending.gatewayConnID = connID;
    pending.gatewayClientConnID = rsp->gatewayConnID;
    pending.sceneConnID = sceneConn;
    m_pendingLogins[rsp->userID] = pending;

    LOG_INFO("UserLogin: userID=%llu gatewayConn=%u sceneConn=%u",
             rsp->userID, connID, sceneConn);

    ConnID recConn = FindSubServer(SubServerType::RECORD);
    if (recConn != INVALID_CONN_ID)
    {
        UserID uid = rsp->userID;
        m_server.SendMsg(recConn, (uint16_t)InternalMsgID::REC_LOAD_USER_REQ,
                         reinterpret_cast<char*>(&uid), sizeof(uid));
    }
    else
    {
        SendLoginFailToGateway(connID, rsp->gatewayConnID, -1);
        m_pendingLogins.erase(rsp->userID);
    }
}

void SuperServer::OnLoadUserRsp(ConnID /*connID*/, const char* data, uint16_t len)
{
    if (len < sizeof(Msg_REC_LoadUserRsp))
    {
        return;
    }
    const auto* hdr = reinterpret_cast<const Msg_REC_LoadUserRsp*>(data);
    auto pit = m_pendingLogins.find(hdr->userID);
    if (pit == m_pendingLogins.end())
    {
        return;
    }

    if (hdr->code != 0 || len < sizeof(Msg_REC_LoadUserRsp) + sizeof(UserBaseWire))
    {
        SendLoginFailToGateway(pit->second.gatewayConnID, pit->second.gatewayClientConnID, hdr->code);
        m_pendingLogins.erase(pit);
        return;
    }

    const auto* wire = reinterpret_cast<const UserBaseWire*>(data + sizeof(Msg_REC_LoadUserRsp));

    Msg_SCE_UserEnterReq enter{};
    enter.userID = wire->userID;
    enter.mapID = wire->mapID ? wire->mapID : 1001;
    enter.x = wire->posX;
    enter.y = wire->posY;
    enter.z = wire->posZ;
    enter.gatewayClientConnID = pit->second.gatewayClientConnID;
    snprintf(enter.name, sizeof(enter.name), "%s", wire->name);
    enter.level = wire->level;
    enter.vocation = wire->vocation;
    enter.sex = wire->sex;
    enter.hp = wire->hp;
    enter.maxHP = wire->maxHP;
    enter.mp = wire->mp;
    enter.maxMP = wire->maxMP;
    enter.gold = wire->gold;

    pit->second.userData = *wire;

    UserProxy proxy;
    proxy.userID = wire->userID;
    proxy.gatewayConnID = pit->second.gatewayConnID;
    proxy.gatewayClientConnID = pit->second.gatewayClientConnID;
    proxy.sceneConnID = pit->second.sceneConnID;
    m_users[wire->userID] = proxy;

    m_server.SendMsg(pit->second.sceneConnID, (uint16_t)InternalMsgID::SCE_USER_ENTER_REQ,
                     reinterpret_cast<char*>(&enter), sizeof(enter));
}

void SuperServer::OnUserEnterRsp(ConnID /*connID*/, const char* data, uint16_t len)
{
    if (len < sizeof(Msg_SCE_UserEnterRsp))
    {
        return;
    }
    const auto* rsp = reinterpret_cast<const Msg_SCE_UserEnterRsp*>(data);
    auto pit = m_pendingLogins.find(rsp->userID);
    if (pit == m_pendingLogins.end())
    {
        return;
    }

    Msg_GW_UserLoginRsp gwRsp{};
    gwRsp.code = rsp->code;
    gwRsp.gatewayClientConnID = rsp->gatewayClientConnID;
    gwRsp.userID = rsp->userID;
    gwRsp.mapID = rsp->mapID;
    if (rsp->code == 0)
    {
        const auto& w = pit->second.userData;
        gwRsp.x = w.posX;
        gwRsp.y = w.posY;
        gwRsp.z = w.posZ;
        gwRsp.level = w.level;
        gwRsp.hp = w.hp;
        gwRsp.maxHP = w.maxHP;
        gwRsp.mp = w.mp;
        gwRsp.maxMP = w.maxMP;
        snprintf(gwRsp.name, sizeof(gwRsp.name), "%s", w.name);
    }

    auto rit = m_users.find(rsp->userID);
    if (rit != m_users.end())
    {
        m_server.SendMsg(rit->second.gatewayConnID, (uint16_t)InternalMsgID::GW_USER_LOGIN_RSP,
                         reinterpret_cast<char*>(&gwRsp), sizeof(gwRsp));
    }

    if (rsp->code != 0)
    {
        m_users.erase(rsp->userID);
    }
    m_pendingLogins.erase(pit);
}

void SuperServer::SendLoginFailToGateway(ConnID gatewayConnID, uint32_t clientConnID, int32_t code)
{
    Msg_GW_UserLoginRsp gwRsp{};
    gwRsp.code = code;
    gwRsp.gatewayClientConnID = clientConnID;
    m_server.SendMsg(gatewayConnID, (uint16_t)InternalMsgID::GW_USER_LOGIN_RSP,
                     reinterpret_cast<char*>(&gwRsp), sizeof(gwRsp));
}

void SuperServer::OnKickUser(ConnID /*connID*/, const char* data, uint16_t len)
{
    if (len < sizeof(UserID))
    {
        return;
    }
    UserID uid = *reinterpret_cast<const UserID*>(data);
    auto it = m_users.find(uid);
    if (it == m_users.end())
    {
        return;
    }
    m_server.SendMsg(it->second.gatewayConnID, (uint16_t)InternalMsgID::GW_KICK_CLIENT,
                     reinterpret_cast<char*>(&it->second.gatewayClientConnID), sizeof(uint32_t));
    m_users.erase(it);
}

void SuperServer::CheckHeartbeat()
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

ConnID SuperServer::FindSubServer(SubServerType type)
{
    for (auto& [cid, info] : m_servers)
    {
        if (info.type == type && info.alive)
        {
            return cid;
        }
    }
    return INVALID_CONN_ID;
}

ConnID SuperServer::FindSceneServer()
{
    return FindSubServer(SubServerType::SCENE);
}

void SuperServer::RemoveSubServer(ConnID connID)
{
    m_servers.erase(connID);
}
