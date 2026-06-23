/**
 * @file    RecordServer.cpp
 * @brief   RecordServer 数据持久化与内部消息处理实现
 */

#include "RecordServer.h"
#include "RecordInternMsgRegister.h"
#include "../sdk/net/MsgIngress.h"
#include "../sdk/net/NetTls.h"
#include "RecordCharService.h"
#include "../sdk/util/LoginFlowLog.h"
#include "../sdk/util/LoginFlowTimeouts.h"
#include "../sdk/util/ServiceHealthMetrics.h"
#include "../sdk/util/LoginFlowTimeouts.h"
#include "../sdk/util/ServerBootstrap.h"

#include <cstring>
#include <unordered_set>
#include <vector>

RecordServer::RecordServer()
    : m_server(this)
    , m_superClient(this)
    , m_db(nullptr)
    , m_externSender(m_superClient, SubServerType::RECORD, 0)
{
}

namespace
{
} // namespace

RecordServer::~RecordServer()
{
    if (m_db)
    {
        mysql_close(m_db);
    }
}

bool RecordServer::Init(const std::string& ip, uint16_t port,
                        const ServerConfig& cfg, const ServerList& list, uint32_t selfId)
{
    Logger::Instance().SetServerName("RecordServer");
    LOG_INFO("存档服启动中: %s:%d", ip.c_str(), port);
    if (const ServerEntry* self = list.find(SubServerType::RECORD, selfId))
        m_self = *self;
    m_externSender.setSelfId(m_self.id ? m_self.id : selfId);
    ServerBootstrap::bindRemoteLog(m_externSender, SubServerType::RECORD);
    wireTlsServer(m_server);
    if (!m_server.Start(ip, port))
    {
        LOG_FATAL("存档服监听启动失败");
        return false;
    }
    if (!InitDB(cfg))
    {
        LOG_FATAL("数据库初始化失败");
        return false;
    }

    wireTlsClient(m_superClient);
    m_superIP = cfg.superIP;
    m_superPort = static_cast<uint16_t>(cfg.superPort);
    m_superClient.Connect(m_superIP, m_superPort);

    registerHandlers();

    TimerMgr::Instance().Register(500, 0, [this] { RegisterToSuper(); });
    TimerMgr::Instance().Register(10000, 10000, [this] { sendHeartbeat(); });
    TimerMgr::Instance().Register(10000, 10000, [this] { tryReconnectSuper(); });
    TimerMgr::Instance().Register(VERIFY_TOKEN_CLEANUP_POLL_MS, VERIFY_TOKEN_CLEANUP_POLL_MS,
                                  [this] { CleanupPendingVerifyTokenTimeout(); });
    // 每 60 秒自动保存所有脏数据
    TimerMgr::Instance().Register(60000, 60000, [this] { autoSaveAll(); });
    LOG_INFO("存档服启动完成");
    return true;
}

void RecordServer::Run()
{
    while (true)
    {
        m_server.Poll(10);
        TimerMgr::Instance().Update();
        m_superClient.Poll(0);
        drainSaveQueue();
    }
}

void RecordServer::OnConnect(ConnID id)
{
    LOG_INFO("内部连接建立: conn=%u", id);
}

void RecordServer::OnDisconnect(ConnID id)
{
    LOG_WARN("内部连接断开: conn=%u", id);
    if (!m_superClient.canSend())
        failAllPendingVerifyTokens();
}

void RecordServer::tryReconnectSuper()
{
    const uint64_t nowMs = TimerMgr::NowMs();
    if (m_superClient.IsConnected() && m_superClient.canSend())
    {
        m_superTlsStuckSinceMs = 0;
        m_superRetryDelayMs = 1000;
        return;
    }
    if (m_superClient.IsConnected() && !m_superClient.canSend())
    {
        if (m_superTlsStuckSinceMs == 0)
            m_superTlsStuckSinceMs = nowMs;
        if (nowMs - m_superTlsStuckSinceMs < EXTERNAL_TLS_STUCK_MS)
            return;
        LOG_WARN("超级服 TLS 半开连接强制断开: %s:%u", m_superIP.c_str(), m_superPort);
        m_superClient.Disconnect();
        m_superTlsStuckSinceMs = 0;
    }
    if (nowMs < m_superNextRetryMs)
        return;

    ServiceHealthMetrics::instance().incReconnectAttempt();
    m_superClient.Disconnect();
    wireTlsClient(m_superClient);
    if (!m_superClient.Connect(m_superIP, m_superPort))
    {
        LOG_WARN("超级服重连失败: %s:%u", m_superIP.c_str(), m_superPort);
        m_superRetryDelayMs = std::min(m_superRetryDelayMs * 2u, 30000u);
    }
    else
    {
        LOG_INFO("超级服重连已发起: %s:%u", m_superIP.c_str(), m_superPort);
        m_superRetryDelayMs = 1000;
        RegisterToSuper();
    }
    m_superNextRetryMs = nowMs + m_superRetryDelayMs;
}

void RecordServer::failAllPendingVerifyTokens()
{
    if (m_pendingVerifyToken.empty())
        return;
    for (const auto& [seq, pending] : m_pendingVerifyToken)
    {
        Msg_REC_ValidateTokenRsp rsp{};
        rsp.code = 1;
        rsp.accid = 0;
        rsp.gatewayConnID = pending.gatewayConnID;
        m_server.SendMsg(pending.replyConn,
                         static_cast<uint16_t>(InternalMsgID::REC_VALIDATE_TOKEN_RSP),
                         reinterpret_cast<char*>(&rsp), sizeof(rsp));
        LOG_WARN("超级服断开，票据校验失败: seq=%u gatewayConn=%u", seq, pending.gatewayConnID);
        logLoginFlow(LoginFlowPhase::GATEWAY_AUTH, 0, 0, pending.gatewayConnID, 1,
                     "Super断开校验失败");
    }
    m_pendingVerifyToken.clear();
}

void RecordServer::OnMessage(ConnID id, uint8_t module, uint8_t sub, const char* data, uint16_t len)
{
    MsgIngress::dispatchInternal(id, module, sub, data, len);
}

bool RecordServer::InitDB(const ServerConfig& cfg)
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
    LOG_INFO("数据库连接成功: %s:%d/%s", cfg.dbHost.c_str(), cfg.dbPort, cfg.dbName.c_str());
    return true;
}

void RecordServer::registerHandlers()
{
    RecordInternMsgRegister(*this);
}

void RecordServer::RegisterToSuper()
{
    if (!m_superClient.canSend())
        return;
    Msg_S2S_Register reg{};
    reg.serverType = (uint8_t)SubServerType::RECORD;
    reg.serverID = m_self.id;
    copyToWire(reg.ip, sizeof(reg.ip), m_self.ip.c_str());
    reg.port = m_self.port;
    copyToWire(reg.name, sizeof(reg.name), m_self.name.c_str());
    m_superClient.SendMsg((uint16_t)InternalMsgID::S2S_REGISTER_REQ,
                          reinterpret_cast<char*>(&reg), sizeof(reg));
}

void RecordServer::sendHeartbeat()
{
    if (!m_superClient.canSend())
        return;
    Msg_S2S_Heartbeat hb{};
    hb.seq = ++m_hbSeq;
    hb.timestamp = TimerMgr::NowMs();
    m_superClient.SendMsg((uint16_t)InternalMsgID::S2S_HEARTBEAT,
                          reinterpret_cast<char*>(&hb), sizeof(hb));
}

void RecordServer::onLoadUser(ConnID /*fromConn*/, const char* data, uint16_t len)
{
    // Super 经 Record→Super 出站连接下发 REC_LOAD_USER_REQ，须同连接回包（m_superClient）
    if (len < sizeof(UserID))
    {
        return;
    }
    UserID rid = *reinterpret_cast<const UserID*>(data);

    if (!m_userManager.contains(rid))
    {
        loadUserFromDb(rid);
    }

    Msg_REC_LoadUserRsp hdr{};
    hdr.userID = rid;
    auto user = m_userManager.findUser(rid);
    if (!user)
    {
        hdr.code = -1;
        m_superClient.SendMsg((uint16_t)InternalMsgID::REC_LOAD_USER_RSP,
                              reinterpret_cast<char*>(&hdr), sizeof(hdr));
        logLoginFlow(LoginFlowPhase::SUPER_ENTER, 0, rid, 0, hdr.code, "Record加载角色");
        return;
    }

    hdr.code = 0;
    const auto& base = user->Base();
    UserBaseWire wire{};
    wire.userID = base.userID;
    copyToWire(wire.name, sizeof(wire.name), base.name.c_str());
    wire.level = base.level;
    wire.vocation = base.vocation;
    wire.sex = base.sex;
    wire.mapID = base.mapID ? base.mapID : 1001;
    wire.posX = base.posX;
    wire.posY = base.posY;
    wire.posZ = base.posZ;
    wire.hp = base.hp;
    wire.maxHP = base.maxHP;
    wire.mp = base.mp;
    wire.maxMP = base.maxMP;
    wire.gold = base.gold;

    std::vector<char> buf(sizeof(hdr) + sizeof(wire));
    memcpy(buf.data(), &hdr, sizeof(hdr));
    memcpy(buf.data() + sizeof(hdr), &wire, sizeof(wire));
    m_superClient.SendMsg((uint16_t)InternalMsgID::REC_LOAD_USER_RSP,
                          buf.data(), static_cast<uint16_t>(buf.size()));
    logLoginFlow(LoginFlowPhase::SUPER_ENTER, 0, rid, 0, 0, "Record加载角色");
}

void RecordServer::onSaveUser(ConnID fromConn, const char* data, uint16_t len)
{
    // Scene 直连 Record Listen 口（m_server）发 REC_SAVE_USER_REQ，须同连接回包
    UserID rid = INVALID_USER_ID;

    if (len >= sizeof(Msg_REC_SaveUserReq))
    {
        const auto* req = reinterpret_cast<const Msg_REC_SaveUserReq*>(data);
        rid = req->userID;

        auto user = m_userManager.findUser(rid);
        if (!user)
        {
            UserBase base;
            applyUserBaseWire(base, req->wire);
            user = RecordUser::create(base);
            user->init();
            m_userManager.addUser(rid, user);
        }
        else
        {
            applyUserBaseWire(user->Base(), req->wire);
            user->markDirty();
        }
    }
    else if (len >= sizeof(UserID))
    {
        rid = *reinterpret_cast<const UserID*>(data);
    }
    else
    {
        return;
    }

    if (m_saveQueue.size() >= MAX_SAVE_QUEUE_DEPTH)
    {
        LOG_WARN("存档写库队列已满，拒绝入队: userID=%llu", static_cast<unsigned long long>(rid));
        return;
    }

    SaveQueueItem item{};
    item.kind = SaveQueueKind::USER;
    item.userId = rid;
    item.replyConn = fromConn;
    m_saveQueue.push_back(std::move(item));

    Msg_REC_LoadUserRsp rsp{};
    rsp.code = 0;
    rsp.userID = rid;
    if (!m_server.SendMsg(fromConn, static_cast<uint16_t>(InternalMsgID::REC_SAVE_USER_RSP),
                          reinterpret_cast<char*>(&rsp), sizeof(rsp)))
        ServiceHealthMetrics::instance().incSendMsgFail();
}

void RecordServer::onRelationPreloadReq(ConnID fromConn, const char* /*data*/, uint16_t /*len*/)
{
    RelationStore store(m_db);
    std::vector<RelationRow> rows;
    std::vector<char> buf;
    if (!store.preloadAll(rows))
    {
        RelationStore::encodePreloadRsp(-1, rows, buf);
    }
    else
    {
        RelationStore::encodePreloadRsp(0, rows, buf);
        LOG_INFO("关系数据预加载完成: 下发 Session 行数=%zu", rows.size());
    }
    m_server.SendMsg(fromConn, (uint16_t)InternalMsgID::REC_RELATION_PRELOAD_RSP,
                     buf.data(), static_cast<uint16_t>(buf.size()));
}

void RecordServer::onRelationLoadReq(ConnID fromConn, const char* data, uint16_t len)
{
    if (len < sizeof(UserID))
        return;

    const UserID uid = *reinterpret_cast<const UserID*>(data);
    RelationStore store(m_db);
    RelationRow row;
    std::vector<char> buf;
    if (!store.loadOne(uid, row))
        RelationStore::encodeLoadRsp(-1, uid, nullptr, buf);
    else
        RelationStore::encodeLoadRsp(0, uid, &row, buf);

    m_server.SendMsg(fromConn, (uint16_t)InternalMsgID::REC_RELATION_LOAD_RSP,
                     buf.data(), static_cast<uint16_t>(buf.size()));
}

void RecordServer::onRelationSaveReq(ConnID fromConn, const char* data, uint16_t len)
{
    RelationRow row;
    Msg_REC_RelationSaveRsp rsp{};
    if (!RelationStore::decodeSaveReq(data, len, row))
    {
        rsp.code = -1;
        m_server.SendMsg(fromConn, (uint16_t)InternalMsgID::REC_RELATION_SAVE_RSP,
                         reinterpret_cast<char*>(&rsp), sizeof(rsp));
        return;
    }

    rsp.userID = row.userID;
    if (m_saveQueue.size() >= MAX_SAVE_QUEUE_DEPTH)
    {
        rsp.code = -1;
        LOG_WARN("关系写库队列已满: userID=%llu", static_cast<unsigned long long>(row.userID));
        m_server.SendMsg(fromConn, (uint16_t)InternalMsgID::REC_RELATION_SAVE_RSP,
                         reinterpret_cast<char*>(&rsp), sizeof(rsp));
        return;
    }

    SaveQueueItem item{};
    item.kind = SaveQueueKind::RELATION;
    item.userId = row.userID;
    item.replyConn = fromConn;
    item.payload.assign(data, data + len);
    m_saveQueue.push_back(std::move(item));

    rsp.code = 0;
    m_server.SendMsg(fromConn, (uint16_t)InternalMsgID::REC_RELATION_SAVE_RSP,
                     reinterpret_cast<char*>(&rsp), sizeof(rsp));
}

void RecordServer::drainSaveQueue()
{
    if (m_saveQueue.empty())
        return;

    SaveQueueItem item = std::move(m_saveQueue.front());
    m_saveQueue.pop_front();

    if (item.kind == SaveQueueKind::USER)
    {
        saveUserToDb(item.userId);
        return;
    }

    RelationStore store(m_db);
    RelationRow row;
    if (RelationStore::decodeSaveReq(item.payload.data(),
                                     static_cast<uint16_t>(item.payload.size()), row))
    {
        if (!store.saveOne(row))
            LOG_WARN("关系异步写库失败: userID=%llu", static_cast<unsigned long long>(row.userID));
    }
}

void RecordServer::loadUserFromDb(UserID rid)
{
    char sql[256];
    snprintf(sql, sizeof(sql),
             "SELECT user_id,name,level,vocation,sex,map_id,pos_x,pos_y,pos_z,"
             "hp,max_hp,mp,max_mp,gold"
             " FROM CharBase WHERE user_id=%" PRIu64 " LIMIT 1",
             rid);
    if (mysql_query(m_db, sql) != 0)
    {
        LOG_ERR("加载用户 SQL 失败: %s", mysql_error(m_db));
        return;
    }
    MYSQL_RES* res = mysql_store_result(m_db);
    MYSQL_ROW row = res ? mysql_fetch_row(res) : nullptr;
    if (row)
    {
        UserBase base;
        base.userID = rid;
        base.name = row[1] ? row[1] : "";
        base.level = row[2] ? (uint32_t)atoi(row[2]) : 1;
        base.vocation = row[3] ? (uint32_t)atoi(row[3]) : 0;
        base.sex = row[4] ? (uint32_t)atoi(row[4]) : 0;
        base.mapID = row[5] ? (uint32_t)atoi(row[5]) : 0;
        base.posX = row[6] ? (float)atof(row[6]) : 0.f;
        base.posY = row[7] ? (float)atof(row[7]) : 0.f;
        base.posZ = row[8] ? (float)atof(row[8]) : 0.f;
        base.hp = row[9] ? (uint32_t)atoi(row[9]) : 100;
        base.maxHP = row[10] ? (uint32_t)atoi(row[10]) : 100;
        base.mp = row[11] ? (uint32_t)atoi(row[11]) : 100;
        base.maxMP = row[12] ? (uint32_t)atoi(row[12]) : 100;
        base.gold = row[13] ? (uint64_t)strtoull(row[13], nullptr, 10) : 0;
        auto user = RecordUser::create(base);
        user->init();
        user->load();
        m_userManager.addUser(rid, user);
        LOG_DEBUG("从数据库加载用户: userID=%llu name=%s", rid, base.name.c_str());
    }
    if (res)
    {
        mysql_free_result(res);
    }
}

void RecordServer::saveUserToDb(UserID rid)
{
    auto user = m_userManager.findUser(rid);
    if (!user || !user->needSave())
    {
        return;
    }
    user->save();
    const auto& base = user->Base();
    char sql[768];
    snprintf(sql, sizeof(sql),
             "INSERT INTO CharBase (user_id,name,level,vocation,sex,map_id,"
             "pos_x,pos_y,pos_z,hp,max_hp,mp,max_mp,gold)"
             " VALUES (%" PRIu64 ",'%s',%u,%u,%u,%u,%.2f,%.2f,%.2f,%u,%u,%u,%u,%" PRIu64 ")"
             " ON DUPLICATE KEY UPDATE name=VALUES(name),level=VALUES(level),"
             " vocation=VALUES(vocation),sex=VALUES(sex),map_id=VALUES(map_id),"
             " pos_x=VALUES(pos_x),pos_y=VALUES(pos_y),pos_z=VALUES(pos_z),"
             " hp=VALUES(hp),max_hp=VALUES(max_hp),mp=VALUES(mp),"
             " max_mp=VALUES(max_mp),gold=VALUES(gold)",
             rid, base.name.c_str(), base.level, base.vocation, base.sex, base.mapID,
             base.posX, base.posY, base.posZ, base.hp, base.maxHP, base.mp, base.maxMP, base.gold);
    if (mysql_query(m_db, sql) != 0)
    {
        LOG_ERR("保存用户 SQL 失败: %s", mysql_error(m_db));
    }
    else
    {
        LOG_DEBUG("用户已写入数据库: userID=%llu", rid);
    }
}

void RecordServer::autoSaveAll()
{
    size_t savedCount = 0;
    m_userManager.forEach([this, &savedCount](UserID rid, RecordUser& user) {
        if (!user.needSave())
            return;
        saveUserToDb(rid);
        ++savedCount;
    });
    if (savedCount > 0)
        LOG_INFO("自动存档完成: 已保存用户=%zu（跳过无变更）", savedCount);
}

void RecordServer::onValidateTokenReq(ConnID fromConn, const Msg_REC_ValidateTokenReq& req)
{
    Msg_Login_VerifyTokenReq verifyReq{};
    verifyReq.requestSeq = ++m_loginVerifySeq;
    copyToWire(verifyReq.loginToken, sizeof(verifyReq.loginToken), req.loginToken);
    verifyReq.zoneId = req.zoneId;
    verifyReq.gameType = req.gameType;

    PendingVerifyToken pending{};
    pending.replyConn = fromConn;
    pending.gatewayConnID = req.gatewayConnID;
    pending.createdAtMs = TimerMgr::NowMs();
    m_pendingVerifyToken[verifyReq.requestSeq] = pending;

    // 经 Super 裸转发 LOGIN_VERIFY_TOKEN_REQ（与网关注册同级），避免 EXT_GAMEZONE 在连接抖动时丢包
    if (!m_superClient.canSend() ||
        !m_superClient.SendMsg(static_cast<uint16_t>(InternalMsgID::LOGIN_VERIFY_TOKEN_REQ),
                               reinterpret_cast<const char*>(&verifyReq), sizeof(verifyReq)))
    {
        m_pendingVerifyToken.erase(verifyReq.requestSeq);
        Msg_REC_ValidateTokenRsp failRsp{};
        failRsp.code = 1;
        failRsp.accid = 0;
        failRsp.gatewayConnID = req.gatewayConnID;
        m_server.SendMsg(fromConn, static_cast<uint16_t>(InternalMsgID::REC_VALIDATE_TOKEN_RSP),
                         reinterpret_cast<char*>(&failRsp), sizeof(failRsp));
        logLoginFlow(LoginFlowPhase::GATEWAY_AUTH, 0, 0, req.gatewayConnID, 1,
                     "转发Login失败");
        return;
    }

    logLoginFlow(LoginFlowPhase::GATEWAY_AUTH, 0, 0, req.gatewayConnID, 0,
                 "Record转发Login校验");
}

void RecordServer::onLoginVerifyTokenRsp(ConnID /*fromConn*/, const Msg_Login_VerifyTokenRsp& rsp)
{
    auto it = m_pendingVerifyToken.find(rsp.requestSeq);
    if (it == m_pendingVerifyToken.end())
        return;

    Msg_REC_ValidateTokenRsp out{};
    out.code = rsp.code;
    out.accid = rsp.accid;
    out.gatewayConnID = it->second.gatewayConnID;
    m_server.SendMsg(it->second.replyConn,
                     static_cast<uint16_t>(InternalMsgID::REC_VALIDATE_TOKEN_RSP),
                     reinterpret_cast<char*>(&out), sizeof(out));
    logLoginFlow(LoginFlowPhase::GATEWAY_AUTH, rsp.accid, 0, out.gatewayConnID, rsp.code,
                 rsp.code == 0 ? "Record校验token成功" : "Record校验token失败");
    m_pendingVerifyToken.erase(it);
}

void RecordServer::CleanupPendingVerifyTokenTimeout()
{
    const uint64_t nowMs = TimerMgr::NowMs();
    if (m_pendingVerifyToken.empty())
        return;

    std::unordered_set<uint32_t> expiredSeq;
    for (const auto& [seq, pending] : m_pendingVerifyToken)
    {
        if (nowMs - pending.createdAtMs >= VERIFY_TOKEN_TIMEOUT_MS)
            expiredSeq.insert(seq);
    }

    for (uint32_t seq : expiredSeq)
    {
        auto it = m_pendingVerifyToken.find(seq);
        if (it == m_pendingVerifyToken.end())
            continue;
        Msg_REC_ValidateTokenRsp rsp{};
        rsp.code = 1;
        rsp.accid = 0;
        rsp.gatewayConnID = it->second.gatewayConnID;
        m_server.SendMsg(it->second.replyConn,
                         static_cast<uint16_t>(InternalMsgID::REC_VALIDATE_TOKEN_RSP),
                         reinterpret_cast<char*>(&rsp), sizeof(rsp));
        m_pendingVerifyToken.erase(it);
        LOG_WARN("票据校验超时已回收: seq=%u", seq);
        logLoginFlow(LoginFlowPhase::GATEWAY_AUTH, 0, 0, rsp.gatewayConnID, 1,
                     "Login校验超时");
    }
}

void RecordServer::onListCharactersReq(ConnID fromConn, const Msg_REC_ListCharactersReq& req)
{
    Msg_REC_ListCharactersRspHeader hdr{};
    std::vector<Msg_REC_CharacterEntryWire> entries;
    RecordCharService::listCharacters(m_db, req, hdr, entries);
    const size_t bodyLen = sizeof(hdr) + entries.size() * sizeof(Msg_REC_CharacterEntryWire);
    std::vector<char> body(bodyLen);
    std::memcpy(body.data(), &hdr, sizeof(hdr));
    if (!entries.empty())
    {
        std::memcpy(body.data() + sizeof(hdr), entries.data(),
                    entries.size() * sizeof(Msg_REC_CharacterEntryWire));
    }
    m_server.SendMsg(fromConn, static_cast<uint16_t>(InternalMsgID::REC_LIST_CHARACTERS_RSP),
                     body.data(), static_cast<uint16_t>(bodyLen));
}

void RecordServer::onCreateCharacterReq(ConnID fromConn, const Msg_REC_CreateCharacterReq& req)
{
    Msg_REC_CreateCharacterRsp rsp{};
    RecordCharService::createCharacter(m_db, req, rsp);
    if (rsp.code == 0 && rsp.userID != 0)
    {
        Msg_Login_UpdateLastUserReq updateReq{};
        updateReq.accid = req.accid;
        updateReq.userID = rsp.userID;
        m_externSender.sendToLoginServer(
            static_cast<uint16_t>(InternalMsgID::LOGIN_UPDATE_LAST_USER_REQ),
            reinterpret_cast<const char*>(&updateReq), sizeof(updateReq));
    }
    m_server.SendMsg(fromConn, static_cast<uint16_t>(InternalMsgID::REC_CREATE_CHARACTER_RSP),
                     reinterpret_cast<char*>(&rsp), sizeof(rsp));
}
