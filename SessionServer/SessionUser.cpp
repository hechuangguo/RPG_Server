/**
 * @file    SessionUser.cpp
 * @brief  SessionUser 生命周期与 Relation 表读写
 */

#include "SessionUser.h"
#include "../sdk/log/Logger.h"
#include "../sdk/time/TimeUtil.h"
#include <cinttypes>
#include <sstream>
#include <string>

namespace {

std::string joinUserIds(const std::vector<UserID>& ids)
{
    std::ostringstream oss;
    for (size_t i = 0; i < ids.size(); ++i)
    {
        if (i > 0) oss << ',';
        oss << ids[i];
    }
    return oss.str();
}

void parseUserIds(const char* text, std::vector<UserID>& out)
{
    out.clear();
    if (!text || !text[0]) return;
    std::string s(text);
    size_t pos = 0;
    while (pos < s.size())
    {
        size_t comma = s.find(',', pos);
        std::string part = s.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        if (!part.empty())
            out.push_back(static_cast<UserID>(strtoull(part.c_str(), nullptr, 10)));
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
}

/** @brief 将 blob 转为 MySQL 字面量 x'HEX'，空数据为 x'' */
std::string blobToSqlHex(const std::vector<uint8_t>& data)
{
    if (data.empty()) return "x''";
    static const char* kHex = "0123456789ABCDEF";
    std::string out;
    out.reserve(3 + data.size() * 2);
    out += "x'";
    for (uint8_t b : data)
    {
        out += kHex[b >> 4];
        out += kHex[b & 0x0F];
    }
    out += '\'';
    return out;
}

void loadBlobFromRow(const char* ptr, unsigned long len, std::vector<uint8_t>& out)
{
    out.clear();
    if (!ptr || len == 0) return;
    out.assign(reinterpret_cast<const uint8_t*>(ptr),
               reinterpret_cast<const uint8_t*>(ptr) + len);
}

} // namespace

std::shared_ptr<SessionUser> SessionUser::create(const UserBase& base)
{
    auto user = std::shared_ptr<SessionUser>(new SessionUser(base));
    LOG_DEBUG("SessionUser::create userID=%llu", base.userID);
    return user;
}

SessionUser::SessionUser(const UserBase& base)
    : IUser(base)
{
    m_social.userID = base.userID;
}

bool SessionUser::init()
{
    if (m_initialized) return true;

    m_social.userID   = GetID();
    m_dirty           = false;
    m_lastDayStartMs  = TimeUtil::StartOfDay(TimeUtil::UnixMs());
    m_initialized     = true;
    SetState(UserState::LOADING);

    LOG_DEBUG("SessionUser::init userID=%llu", GetID());
    return true;
}

bool SessionUser::onOnline()
{
    if (!m_initialized && !init()) return false;

    SetState(UserState::ONLINE);
    LOG_INFO("SessionUser::onOnline userID=%llu", GetID());
    return true;
}

bool SessionUser::onOffline()
{
    SetState(UserState::OFFLINE);
    LOG_INFO("SessionUser::onOffline userID=%llu", GetID());
    return true;
}

void SessionUser::applyRelationFromDbRow(const char* friendsJson, const char* blacklistJson,
                                         const char* guildIdStr, const char* teamIdStr,
                                         const char* binaryPtr, unsigned long binaryLen)
{
    parseUserIds(friendsJson, m_social.friends);
    parseUserIds(blacklistJson, m_social.blackList);
    m_social.guildId = guildIdStr ? strtoull(guildIdStr, nullptr, 10) : 0;
    m_social.teamId  = teamIdStr ? static_cast<uint32_t>(strtoul(teamIdStr, nullptr, 10)) : 0;
    loadBlobFromRow(binaryPtr, binaryLen, m_social.binary);
    m_social.userID = GetID();
    m_dirty = false;
}

bool SessionUser::load(MYSQL* db)
{
    if (!db) return false;
    if (!m_initialized && !init()) return false;

    char sql[256];
    snprintf(sql, sizeof(sql),
             "SELECT friends_json,blacklist_json,guild_id,team_id,`binary`"
             " FROM Relation WHERE user_id=%" PRIu64 " LIMIT 1", GetID());

    if (mysql_query(db, sql) != 0)
    {
        LOG_ERR("SessionUser::load SQL err: %s", mysql_error(db));
        return false;
    }

    MYSQL_RES* res = mysql_store_result(db);
    MYSQL_ROW  row = res ? mysql_fetch_row(res) : nullptr;
    unsigned long* lengths = res && row ? mysql_fetch_lengths(res) : nullptr;
    if (row)
    {
        const unsigned long binaryLen = lengths ? lengths[4] : 0;
        applyRelationFromDbRow(row[0], row[1], row[2], row[3], row[4], binaryLen);
    }
    else
    {
        char ins[320];
        snprintf(ins, sizeof(ins),
                 "INSERT INTO Relation (user_id,friends_json,blacklist_json,guild_id,team_id,`binary`)"
                 " VALUES (%" PRIu64 ",'','',0,0,x'')", GetID());
        if (mysql_query(db, ins) != 0)
            LOG_WARN("SessionUser::load insert relation err: %s", mysql_error(db));
        m_social.binary.clear();
    }

    if (res) mysql_free_result(res);
    m_social.userID = GetID();
    m_dirty = false;
    LOG_DEBUG("SessionUser::load userID=%llu friends=%zu binary=%zu",
              GetID(), m_social.friends.size(), m_social.binary.size());
    return true;
}

bool SessionUser::save(MYSQL* db)
{
    if (!db) return false;
    if (!needSave()) return true;

    const std::string friends   = joinUserIds(m_social.friends);
    const std::string blacklist = joinUserIds(m_social.blackList);
    const std::string binaryLit = blobToSqlHex(m_social.binary);

    std::ostringstream sql;
    sql << "INSERT INTO Relation (user_id,friends_json,blacklist_json,guild_id,team_id,`binary`)"
        << " VALUES (" << GetID() << ",'" << friends << "','" << blacklist << "',"
        << m_social.guildId << "," << m_social.teamId << "," << binaryLit << ")"
        << " ON DUPLICATE KEY UPDATE friends_json=VALUES(friends_json),"
        << " blacklist_json=VALUES(blacklist_json),"
        << " guild_id=VALUES(guild_id), team_id=VALUES(team_id),"
        << " `binary`=VALUES(`binary`)";

    const std::string q = sql.str();
    if (mysql_query(db, q.c_str()) != 0)
    {
        LOG_ERR("SessionUser::save SQL err: %s", mysql_error(db));
        return false;
    }

    m_dirty = false;
    LOG_DEBUG("SessionUser::save userID=%llu → Relation binary=%zu",
              GetID(), m_social.binary.size());
    return true;
}

bool SessionUser::needSave() const
{
    return m_dirty;
}

void SessionUser::loop(uint64_t nowMs)
{
    if (GetState() != UserState::ONLINE) return;

    OnTick(nowMs);

    const int64_t dayStart = TimeUtil::StartOfDay(static_cast<int64_t>(nowMs));
    if (m_lastDayStartMs >= 0 && dayStart != m_lastDayStartMs)
        onMidnight();
    m_lastDayStartMs = dayStart;
}

void SessionUser::onMidnight()
{
    LOG_INFO("SessionUser::onMidnight userID=%llu", GetID());
}
