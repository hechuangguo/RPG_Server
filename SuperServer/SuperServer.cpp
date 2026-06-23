/**
 * @file    SuperServer.cpp
 * @brief   SuperServer 业务实现与消息处理逻辑
 */

#include "SuperServer.h"
#include "SuperInternMsgRegister.h"
#include "SuperZoneStatusMsg.h"
#include "../sdk/net/MsgIngress.h"
#include "../sdk/net/NetTls.h"
#include "SuperExternRouter.h"
#include "SuperLoginMsg.h"
#include "LoginExternOutbox.h"
#include "SuperLoggerMsg.h"
#include "SuperGlobalMsg.h"
#include "SuperZoneMsg.h"
#include "SuperZoneStatusMsg.h"
#include "../sdk/util/ServerBootstrap.h"
#include "../sdk/util/WireStringUtil.h"
#include "../sdk/util/LoginSpawnConfig.h"
#include "../sdk/util/LoginFlowLog.h"
#include "../sdk/util/LoginEnterErrorCode.h"

#include <cstring>
#include <cstdio>
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
    LOG_INFO("超级服启动中: %s:%d zone=%u gameType=%u",
             ip.c_str(), port, cfg.zoneId, cfg.gameType);

    m_zoneId = cfg.zoneId;
    m_gameType = cfg.gameType;

    if (!loadServerList(cfg))
    {
        LOG_FATAL("加载服务器列表失败");
        return false;
    }

    wireTlsServer(m_server);
    if (!m_server.Start(ip, port))
    {
        LOG_FATAL("超级服监听启动失败");
        return false;
    }

    registerHandlers();

    TimerMgr::Instance().Register(30000, 30000, [this] { checkHeartbeat(); });
    TimerMgr::Instance().Register(10000, 10000, [this] { checkPendingLoginTimeouts(); });
    TimerMgr::Instance().Register(60000, 60000, [this] {
        if (!refreshServerListFromDb())
            LOG_WARN("超级服增量刷新服务器列表失败");
    });
    SuperZoneStatusMsgRegister(*this);
    LOG_INFO("超级服启动完成");
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
        LOG_ERR("数据库连接失败: %s", mysql_error(m_db));
        return false;
    }
    mysql_set_character_set(m_db, "utf8mb4");

    const char* sql = "SELECT server_id, server_type, ip, port, name FROM ServerList";
    if (mysql_query(m_db, sql) != 0)
    {
        LOG_ERR("查询服务器列表失败: %s", mysql_error(m_db));
        return false;
    }
    MYSQL_RES* res = mysql_store_result(m_db);
    if (!res)
    {
        LOG_ERR("服务器列表读取结果失败: %s", mysql_error(m_db));
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
    LOG_INFO("服务器列表加载完成: 条目=%zu", m_serverList.size());
    return true;
}

bool SuperServer::refreshServerListFromDb()
{
    if (!m_db)
        return false;

    const char* sql = "SELECT server_id, server_type, ip, port, name FROM ServerList";
    if (mysql_query(m_db, sql) != 0)
    {
        LOG_WARN("增量刷新服务器列表查询失败: %s", mysql_error(m_db));
        return false;
    }
    MYSQL_RES* res = mysql_store_result(m_db);
    if (!res)
    {
        LOG_WARN("增量刷新服务器列表读取失败: %s", mysql_error(m_db));
        return false;
    }

    ServerList refreshed;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)) != nullptr)
    {
        ServerEntry e;
        e.id   = row[0] ? (uint32_t)strtoul(row[0], nullptr, 10) : 0;
        e.type = (SubServerType)(row[1] ? (uint8_t)strtoul(row[1], nullptr, 10) : 0);
        e.ip   = row[2] ? row[2] : "";
        e.port = row[3] ? (uint16_t)strtoul(row[3], nullptr, 10) : 0;
        e.name = row[4] ? row[4] : "";
        refreshed.add(e);
    }
    mysql_free_result(res);
    m_serverList = std::move(refreshed);
    LOG_INFO("超级服增量刷新服务器列表完成: 条目=%zu", m_serverList.size());
    return true;
}

void SuperServer::Run()
{
    while (true)
    {
        /** 先处理区内消息入队，再刷新外联出站 */
        m_externHub.poll();
        m_server.Poll(10);
        superLoginOnExternTick(*this);
        const SubServerType skipLoginReconnect =
            superLoginHasPendingVerify() ? SubServerType::LOGIN : SubServerType::UNKNOWN;
        m_externHub.tickReconnect(TimerMgr::NowMs(), skipLoginReconnect);
        TimerMgr::Instance().Update();
    }
}

void SuperServer::setupExternalClients(const LoginServerList& list)
{
    /** Super 为外联枢纽：代码侧启用四类连接，具体地址仍来自 loginserverlist.xml */
    ServerBootstrap::initGameZoneExtern(m_externHub, list, SubServerType::UNKNOWN,
                                          true, true, true);
}

void SuperServer::OnConnect(ConnID id)
{
    LOG_INFO("子服连接建立: connID=%u", id);
}

void SuperServer::OnDisconnect(ConnID id)
{
    LOG_WARN("子服连接断开: connID=%u", id);
    removeSubServer(id);
}

void SuperServer::OnMessage(ConnID id, uint8_t module, uint8_t sub, const char* data, uint16_t len)
{
    MsgIngress::dispatchInternal(id, module, sub, data, len);
}

void SuperServer::registerHandlers()
{
    SuperInternMsgRegister(*this);
}

void SuperServer::onRegister(ConnID connID, const Msg_S2S_Register& req)
{
    SubServerInfo info;
    info.connID = connID;
    info.type = (SubServerType)req.serverType;
    info.serverID = req.serverID;
    info.ip = req.ip;
    info.port = req.port;
    info.name = req.name;
    info.alive = true;
    info.lastHeartbeat = TimerMgr::NowMs();
    m_servers[connID] = info;
    LOG_INFO("子服注册成功: type=%d serverID=%u ip=%s port=%d name=%s",
             (int)info.type, info.serverID, info.ip.c_str(), info.port, info.name.c_str());

    char rsp[4] = {0};
    m_server.SendMsg(connID, (uint16_t)InternalMsgID::S2S_REGISTER_RSP, rsp, sizeof(rsp));
}

void SuperServer::onHeartbeat(ConnID connID, const char* data, uint16_t len)
{
    uint32_t seq = 0;
    uint32_t onlineCount = 0;
    if (len >= S2S_HEARTBEAT_BODY_V1)
    {
        const auto* hb = reinterpret_cast<const Msg_S2S_Heartbeat*>(data);
        seq = hb->seq;
        if (len >= sizeof(Msg_S2S_Heartbeat))
            onlineCount = hb->onlineCount;
    }

    auto it = m_servers.find(connID);
    if (it != m_servers.end())
    {
        it->second.lastHeartbeat = TimerMgr::NowMs();
        if (it->second.type == SubServerType::GATEWAY && it->second.alive &&
            len >= sizeof(Msg_S2S_Heartbeat))
        {
            m_gatewayOnline[it->second.serverID] = onlineCount;
        }
    }

    Msg_S2S_Heartbeat ack{};
    ack.seq = seq;
    ack.timestamp = TimerMgr::NowMs();
    m_server.SendMsg(connID, (uint16_t)InternalMsgID::S2S_HEARTBEAT_ACK,
                     reinterpret_cast<char*>(&ack), sizeof(ack));
}

void SuperServer::reportZoneStatusToLogin()
{
    TcpClient* login = m_externHub.client(SubServerType::LOGIN);
    if (!login || !login->canSend())
        return;

    uint32_t onlineTotal = 0;
    uint32_t gatewayCount = 0;
    for (const auto& kv : m_servers)
    {
        if (kv.second.type != SubServerType::GATEWAY || !kv.second.alive)
            continue;
        ++gatewayCount;
        auto git = m_gatewayOnline.find(kv.second.serverID);
        if (git != m_gatewayOnline.end())
            onlineTotal += git->second;
    }

    Msg_Login_ZoneStatusReport report{};
    report.zoneId = m_zoneId;
    report.gameType = m_gameType;
    report.onlineCount = onlineTotal;
    report.gatewayCount = gatewayCount;
    report.alive = (gatewayCount > 0) ? 1 : 0;

    LoginExternOutbox::enqueueZoneStatusReport(report);
}

void SuperServer::onServerListReq(ConnID connID, const char* /*data*/, uint16_t /*len*/)
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
        copyToWire(wire.ip, sizeof(wire.ip), e.ip.c_str());
        wire.port = e.port;
        copyToWire(wire.name, sizeof(wire.name), e.name.c_str());
        memcpy(p, &wire, sizeof(wire));
        p += sizeof(wire);
    }

    m_server.SendMsg(connID, (uint16_t)InternalMsgID::S2S_SERVERLIST_RSP,
                     buf.data(), (uint16_t)buf.size());
    LOG_INFO("已发送服务器列表: connID=%u (entries=%u)", connID, count);
}

void SuperServer::onUserLoginReq(ConnID connID, const Msg_GW_UserEnterReq& req)
{
    const UserID userID = req.userID;
    if (userID == 0)
    {
        sendLoginFailToGateway(connID, req.gatewayClientConnID,
                               static_cast<int32_t>(SuperEnterError::NO_RECORD));
        return;
    }

    kickExistingUserSession(userID);

    const uint64_t nowMs = TimerMgr::NowMs();
    auto pendingIt = m_pendingLogins.find(userID);
    if (pendingIt != m_pendingLogins.end())
    {
        const uint64_t ageMs = nowMs - pendingIt->second.startedAtMs;
        if (ageMs < LOGIN_TXN_LOCK_TIMEOUT_MS)
        {
            const bool sameTxnId = req.loginTxnId != 0 &&
                                   pendingIt->second.loginTxnId == req.loginTxnId;
            const bool sameClientRetry = req.loginTxnId == 0 &&
                                         pendingIt->second.gatewayClientConnID ==
                                             req.gatewayClientConnID;
            if (sameTxnId || sameClientRetry)
            {
                logLoginFlow(LoginFlowPhase::SUPER_ENTER, 0, userID, req.gatewayClientConnID,
                             0, "幂等重复请求重试，重建事务", req.loginTxnId);
                m_pendingLogins.erase(pendingIt);
            }
            else
            {
                sendLoginFailToGateway(connID, req.gatewayClientConnID,
                                       static_cast<int32_t>(SuperEnterError::TXN_IN_PROGRESS));
                logLoginFlow(LoginFlowPhase::SUPER_ENTER, 0, userID, req.gatewayClientConnID,
                             static_cast<int32_t>(SuperEnterError::TXN_IN_PROGRESS),
                             "重复进世界请求（事务进行中）", req.loginTxnId);
                return;
            }
        }
        else
        {
            sendLoginFailToGateway(pendingIt->second.gatewayConnID,
                                   pendingIt->second.gatewayClientConnID,
                                   static_cast<int32_t>(SuperEnterError::TXN_TIMEOUT));
            m_pendingLogins.erase(pendingIt);
        }
    }

    PendingLogin pending{};
    pending.userID = userID;
    pending.gatewayConnID = connID;
    pending.gatewayClientConnID = req.gatewayClientConnID;
    pending.loginTxnId = req.loginTxnId;
    pending.startedAtMs = nowMs;
    pending.phase = LoginTxnPhase::LOAD_USER;
    m_pendingLogins[userID] = pending;

    logLoginFlow(LoginFlowPhase::SUPER_ENTER, 0, userID, req.gatewayClientConnID, 0,
                 "开始加载角色", req.loginTxnId);

    if (m_pendingLogins.size() >= LOGIN_FLOW_ALERT_PENDING_COUNT)
    {
        LOG_WARN("[登录链路] 待完成登录堆积告警: pending=%zu threshold=%u",
                 m_pendingLogins.size(), LOGIN_FLOW_ALERT_PENDING_COUNT);
    }

    ConnID recConn = findSubServer(SubServerType::RECORD);
    if (recConn != INVALID_CONN_ID)
    {
        m_server.SendMsg(recConn, (uint16_t)InternalMsgID::REC_LOAD_USER_REQ,
                         reinterpret_cast<const char*>(&userID), sizeof(userID));
    }
    else
    {
        sendLoginFailToGateway(connID, req.gatewayClientConnID,
                               static_cast<int32_t>(SuperEnterError::NO_RECORD));
        m_pendingLogins.erase(userID);
        logLoginFlow(LoginFlowPhase::SUPER_ENTER, 0, userID, req.gatewayClientConnID,
                     static_cast<int32_t>(SuperEnterError::NO_RECORD),
                     "无可用存档服", req.loginTxnId);
    }
}

void SuperServer::kickExistingUserSession(UserID userID)
{
    auto it = m_users.find(userID);
    if (it == m_users.end())
        return;

    const UserProxy proxy = it->second;
    m_server.SendMsg(proxy.gatewayConnID, (uint16_t)InternalMsgID::GW_KICK_CLIENT,
                     reinterpret_cast<const char*>(&proxy.gatewayClientConnID),
                     sizeof(uint32_t));
    if (proxy.sceneConnID != INVALID_CONN_ID)
    {
        m_server.SendMsg(proxy.sceneConnID, (uint16_t)InternalMsgID::SCE_USER_LEAVE,
                         reinterpret_cast<const char*>(&userID), sizeof(UserID));
    }
    m_users.erase(it);
    logLoginFlow(LoginFlowPhase::SUPER_ENTER, 0, userID, proxy.gatewayClientConnID, 0,
                 "重复登录踢旧会话");
}

void SuperServer::onUserLeaveReq(ConnID /*connID*/, const Msg_GW_UserLeaveReq& req)
{
    const UserID userID = req.userID;
    if (userID == INVALID_USER_ID)
        return;

    LOG_INFO("收到离世界请求: userID=%llu gatewayClientConn=%u",
             userID, req.gatewayClientConnID);

    auto pendingIt = m_pendingLogins.find(userID);
    if (pendingIt != m_pendingLogins.end())
    {
        if (pendingIt->second.sceneConnID != INVALID_CONN_ID)
        {
            m_server.SendMsg(pendingIt->second.sceneConnID,
                             static_cast<uint16_t>(InternalMsgID::SCE_USER_LEAVE),
                             reinterpret_cast<const char*>(&userID), sizeof(UserID));
            LOG_INFO("ENTERING 断线清理: 通知场景服 userID=%llu sceneConn=%u",
                     userID, pendingIt->second.sceneConnID);
        }
        logLoginFlow(LoginFlowPhase::CHAR_LEAVE, 0, userID, req.gatewayClientConnID, 0,
                     "主动离开，取消 pending", pendingIt->second.loginTxnId);
        m_pendingLogins.erase(pendingIt);
    }

    auto userIt = m_users.find(userID);
    if (userIt != m_users.end())
    {
        logLoginFlow(LoginFlowPhase::LOGOUT, 0, userID, req.gatewayClientConnID, 0,
                     "主动离开，清除在线映射");
        m_users.erase(userIt);
    }
}

void SuperServer::checkPendingLoginTimeouts()
{
    const uint64_t nowMs = TimerMgr::NowMs();
    std::vector<UserID> expired;
    expired.reserve(m_pendingLogins.size());

    for (const auto& [uid, pending] : m_pendingLogins)
    {
        if (nowMs - pending.startedAtMs >= LOGIN_TXN_LOCK_TIMEOUT_MS)
            expired.push_back(uid);
    }

    for (UserID uid : expired)
    {
        auto pit = m_pendingLogins.find(uid);
        if (pit == m_pendingLogins.end())
            continue;
        sendLoginFailToGateway(pit->second.gatewayConnID,
                               pit->second.gatewayClientConnID,
                               static_cast<int32_t>(SuperEnterError::TXN_TIMEOUT));
        logLoginFlow(LoginFlowPhase::SUPER_ENTER, 0, uid,
                     pit->second.gatewayClientConnID,
                     static_cast<int32_t>(SuperEnterError::TXN_TIMEOUT),
                     "登录事务超时回滚", pit->second.loginTxnId);
        m_pendingLogins.erase(pit);
    }
}

void SuperServer::onLoadUserRsp(ConnID /*connID*/, const char* data, uint16_t len)
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
        const int32_t code = hdr->code != 0
            ? hdr->code
            : static_cast<int32_t>(SuperEnterError::LOAD_USER_FAILED);
        sendLoginFailToGateway(pit->second.gatewayConnID, pit->second.gatewayClientConnID, code);
        logLoginFlow(LoginFlowPhase::SUPER_ENTER, 0, hdr->userID,
                     pit->second.gatewayClientConnID, code, "加载角色失败",
                     pit->second.loginTxnId);
        m_pendingLogins.erase(pit);
        return;
    }

    const auto* wire = reinterpret_cast<const UserBaseWire*>(data + sizeof(Msg_REC_LoadUserRsp));
    pit->second.userData = *wire;
    pit->second.mapId = wire->mapID ? wire->mapID : DEFAULT_NEWBIE_MAP_ID;
    pit->second.phase = LoginTxnPhase::RESOLVE_MAP;

    ConnID sesConn = findSubServer(SubServerType::SESSION);
    if (sesConn == INVALID_CONN_ID)
    {
        LOG_WARN("用户登录失败: 无会话服可选图 userID=%llu", hdr->userID);
        sendLoginFailToGateway(pit->second.gatewayConnID, pit->second.gatewayClientConnID,
                               static_cast<int32_t>(SuperEnterError::NO_SESSION));
        logLoginFlow(LoginFlowPhase::SUPER_ENTER, 0, hdr->userID,
                     pit->second.gatewayClientConnID,
                     static_cast<int32_t>(SuperEnterError::NO_SESSION), "无会话服",
                     pit->second.loginTxnId);
        m_pendingLogins.erase(pit);
        return;
    }

    Msg_SES_ResolveMapReq req{};
    req.userID = hdr->userID;
    req.mapId = pit->second.mapId;
    pit->second.awaitingMapResolve = true;
    m_server.SendMsg(sesConn, (uint16_t)InternalMsgID::SES_RESOLVE_MAP_REQ,
                     reinterpret_cast<char*>(&req), sizeof(req));
    LOG_INFO("已请求地图解析: map=%u userID=%llu", req.mapId, hdr->userID);
    char detail[48];
    snprintf(detail, sizeof(detail), "加载角色成功 map=%u", pit->second.mapId);
    logLoginFlow(LoginFlowPhase::SUPER_ENTER, 0, hdr->userID,
                 pit->second.gatewayClientConnID, 0, detail, pit->second.loginTxnId);
}

void SuperServer::onResolveMapRsp(ConnID /*connID*/, const Msg_SES_ResolveMapRsp& rsp)
{
    auto pit = m_pendingLogins.find(rsp.userID);
    if (pit == m_pendingLogins.end() || !pit->second.awaitingMapResolve)
        return;

    PendingLogin& pending = pit->second;
    pending.awaitingMapResolve = false;

    if (rsp.code != 0 || rsp.sceneServerId == 0)
    {
        LOG_WARN("用户登录失败: 地图未注册 map=%u userID=%llu", rsp.mapId, rsp.userID);
        sendLoginFailToGateway(pending.gatewayConnID, pending.gatewayClientConnID,
                               static_cast<int32_t>(SuperEnterError::MAP_NOT_REGISTERED));
        logLoginFlow(LoginFlowPhase::SUPER_ENTER, 0, rsp.userID,
                     pending.gatewayClientConnID,
                     static_cast<int32_t>(SuperEnterError::MAP_NOT_REGISTERED),
                     "地图未注册", pending.loginTxnId);
        m_pendingLogins.erase(pit);
        return;
    }

    pending.sceneConnID = findSubServerByServerId(SubServerType::SCENE, rsp.sceneServerId);
    if (pending.sceneConnID == INVALID_CONN_ID)
    {
        LOG_WARN("用户登录失败: 场景服未连接 sceneServerId=%u", rsp.sceneServerId);
        sendLoginFailToGateway(pending.gatewayConnID, pending.gatewayClientConnID,
                               static_cast<int32_t>(SuperEnterError::SCENE_OFFLINE));
        logLoginFlow(LoginFlowPhase::SUPER_ENTER, 0, rsp.userID,
                     pending.gatewayClientConnID,
                     static_cast<int32_t>(SuperEnterError::SCENE_OFFLINE),
                     "场景服未连接", pending.loginTxnId);
        m_pendingLogins.erase(pit);
        return;
    }

    pending.phase = LoginTxnPhase::ENTER_SCENE;
    char detail[64];
    snprintf(detail, sizeof(detail), "地图解析 map=%u scene=%u", rsp.mapId, rsp.sceneServerId);
    logLoginFlow(LoginFlowPhase::SUPER_ENTER, 0, rsp.userID, pending.gatewayClientConnID,
                 0, detail, pending.loginTxnId);
    sendUserEnterToScene(pending);
}

void SuperServer::sendUserEnterToScene(PendingLogin& pending)
{
    const auto& wire = pending.userData;

    UserProxy proxy;
    proxy.userID = wire.userID;
    proxy.gatewayConnID = pending.gatewayConnID;
    proxy.gatewayClientConnID = pending.gatewayClientConnID;
    proxy.sceneConnID = pending.sceneConnID;
    m_users[wire.userID] = proxy;

    Msg_SCE_UserEnterReq enter{};
    enter.userID = wire.userID;
    enter.mapID = pending.mapId;
    enter.x = wire.posX;
    enter.y = wire.posY;
    enter.z = wire.posZ;
    enter.gatewayClientConnID = pending.gatewayClientConnID;
    copyToWire(enter.name, sizeof(enter.name), wire.name);
    enter.level = wire.level;
    enter.vocation = wire.vocation;
    enter.sex = wire.sex;
    enter.hp = wire.hp;
    enter.maxHP = wire.maxHP;
    enter.mp = wire.mp;
    enter.maxMP = wire.maxMP;
    enter.gold = wire.gold;

    m_server.SendMsg(pending.sceneConnID, (uint16_t)InternalMsgID::SCE_USER_ENTER_REQ,
                     reinterpret_cast<char*>(&enter), sizeof(enter));
    LOG_INFO("已发送用户入场: userID=%llu map=%u sceneConn=%u",
             wire.userID, pending.mapId, pending.sceneConnID);
    char detail[64];
    snprintf(detail, sizeof(detail), "下发Scene入场 map=%u sceneConn=%u",
             pending.mapId, pending.sceneConnID);
    logLoginFlow(LoginFlowPhase::SUPER_ENTER, 0, wire.userID, pending.gatewayClientConnID,
                 0, detail, pending.loginTxnId);
}

void SuperServer::onUserEnterRsp(ConnID /*connID*/, const Msg_SCE_UserEnterRsp& rsp)
{
    auto pit = m_pendingLogins.find(rsp.userID);
    if (pit == m_pendingLogins.end())
    {
        return;
    }

    Msg_GW_UserLoginRsp gwRsp{};
    gwRsp.code = rsp.code;
    gwRsp.gatewayClientConnID = rsp.gatewayClientConnID;
    gwRsp.userID = rsp.userID;
    gwRsp.mapID = rsp.mapID;
    if (rsp.code == 0)
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
        copyToWire(gwRsp.name, sizeof(gwRsp.name), w.name);
        auto sit = m_servers.find(pit->second.sceneConnID);
        if (sit != m_servers.end())
            gwRsp.sceneServerId = sit->second.serverID;
    }

    if (rsp.code == 0)
    {
        logLoginFlow(LoginFlowPhase::SCENE_ENTER, 0, rsp.userID,
                     rsp.gatewayClientConnID, 0, "进世界成功");
    }
    else
    {
        logLoginFlow(LoginFlowPhase::SCENE_ENTER, 0, rsp.userID,
                     rsp.gatewayClientConnID, rsp.code, "场景入场失败");
    }

    m_server.SendMsg(pit->second.gatewayConnID, (uint16_t)InternalMsgID::GW_USER_LOGIN_RSP,
                     reinterpret_cast<char*>(&gwRsp), sizeof(gwRsp));

    if (rsp.code != 0)
    {
        m_users.erase(rsp.userID);
    }
    m_pendingLogins.erase(pit);
}

void SuperServer::sendLoginFailToGateway(ConnID gatewayConnID, uint32_t clientConnID, int32_t code)
{
    Msg_GW_UserLoginRsp gwRsp{};
    gwRsp.code = code;
    gwRsp.gatewayClientConnID = clientConnID;
    m_server.SendMsg(gatewayConnID, (uint16_t)InternalMsgID::GW_USER_LOGIN_RSP,
                     reinterpret_cast<char*>(&gwRsp), sizeof(gwRsp));
}

void SuperServer::onKickUser(ConnID /*connID*/, const char* data, uint16_t len)
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

void SuperServer::checkHeartbeat()
{
    uint64_t now = TimerMgr::NowMs();
    for (auto& [cid, info] : m_servers)
    {
        if (now - info.lastHeartbeat > 90000)
        {
            LOG_WARN("子服心跳超时: connID=%u type=%d", cid, (int)info.type);
            info.alive = false;
        }
    }
}

ConnID SuperServer::findSubServer(SubServerType type)
{
    for (auto& [cid, info] : m_servers)
    {
        if (info.type == type && info.alive)
            return cid;
    }
    return INVALID_CONN_ID;
}

ConnID SuperServer::findSubServerByServerId(SubServerType type, uint32_t serverId)
{
    for (auto& [cid, info] : m_servers)
    {
        if (info.type == type && info.serverID == serverId && info.alive)
            return cid;
    }
    return INVALID_CONN_ID;
}

void SuperServer::removeSubServer(ConnID connID)
{
    SubServerType disconnectedType = SubServerType::UNKNOWN;
    auto it = m_servers.find(connID);
    if (it != m_servers.end())
    {
        disconnectedType = it->second.type;
        if (it->second.type == SubServerType::GATEWAY)
            m_gatewayOnline.erase(it->second.serverID);
    }
    m_servers.erase(connID);

    if (disconnectedType == SubServerType::RECORD || disconnectedType == SubServerType::SESSION)
        failPendingLoginsOnSubServerDisconnect(disconnectedType);
}

void SuperServer::failPendingLoginsOnSubServerDisconnect(SubServerType type)
{
    std::vector<UserID> toFail;
    toFail.reserve(m_pendingLogins.size());
    for (const auto& [uid, pending] : m_pendingLogins)
    {
        if (type == SubServerType::RECORD)
            toFail.push_back(uid);
        else if (type == SubServerType::SESSION
                 && (pending.awaitingMapResolve || pending.phase == LoginTxnPhase::RESOLVE_MAP))
            toFail.push_back(uid);
    }

    for (UserID uid : toFail)
    {
        auto pit = m_pendingLogins.find(uid);
        if (pit == m_pendingLogins.end())
            continue;
        const int32_t code = type == SubServerType::RECORD
            ? static_cast<int32_t>(SuperEnterError::LOAD_USER_FAILED)
            : static_cast<int32_t>(SuperEnterError::NO_SESSION);
        sendLoginFailToGateway(pit->second.gatewayConnID, pit->second.gatewayClientConnID, code);
        LOG_WARN("子服断连清理登录事务: userID=%llu type=%d code=%d",
                 static_cast<unsigned long long>(uid), static_cast<int>(type), code);
        m_pendingLogins.erase(pit);
    }
}
