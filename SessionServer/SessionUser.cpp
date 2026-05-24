/**
 * @file    SessionUser.cpp
 * @brief  SessionUser 生命周期与 t_relation 表读写
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

bool SessionUser::load(MYSQL* db)
{
    if (!db) return false;
    if (!m_initialized && !init()) return false;

    char sql[256];
    snprintf(sql, sizeof(sql),
             "SELECT friends_json,blacklist_json,guild_id,team_id"
             " FROM t_relation WHERE user_id=%" PRIu64 " LIMIT 1", GetID());

    if (mysql_query(db, sql) != 0)
    {
        LOG_ERR("SessionUser::load SQL err: %s", mysql_error(db));
        return false;
    }

    MYSQL_RES* res = mysql_store_result(db);
    MYSQL_ROW  row = res ? mysql_fetch_row(res) : nullptr;
    if (row)
    {
        parseUserIds(row[0], m_social.friends);
        parseUserIds(row[1], m_social.blackList);
        m_social.guildId = row[2] ? strtoull(row[2], nullptr, 10) : 0;
        m_social.teamId  = row[3] ? (uint32_t)strtoul(row[3], nullptr, 10) : 0;
    }
    else
    {
        char ins[256];
        snprintf(ins, sizeof(ins),
                 "INSERT INTO t_relation (user_id,friends_json,blacklist_json,guild_id,team_id)"
                 " VALUES (%" PRIu64 ",'','',0,0)", GetID());
        if (mysql_query(db, ins) != 0)
            LOG_WARN("SessionUser::load insert relation err: %s", mysql_error(db));
    }

    if (res) mysql_free_result(res);
    m_social.userID = GetID();
    m_dirty = false;
    LOG_DEBUG("SessionUser::load userID=%llu friends=%zu", GetID(), m_social.friends.size());
    return true;
}

bool SessionUser::save(MYSQL* db)
{
    if (!db) return false;
    if (!needSave()) return true;

    const std::string friends   = joinUserIds(m_social.friends);
    const std::string blacklist = joinUserIds(m_social.blackList);

    char sql[1024];
    snprintf(sql, sizeof(sql),
             "INSERT INTO t_relation (user_id,friends_json,blacklist_json,guild_id,team_id)"
             " VALUES (%" PRIu64 ",'%s','%s',%" PRIu64 ",%u)"
             " ON DUPLICATE KEY UPDATE friends_json=VALUES(friends_json),"
             " blacklist_json=VALUES(blacklist_json),"
             " guild_id=VALUES(guild_id), team_id=VALUES(team_id)",
             GetID(), friends.c_str(), blacklist.c_str(),
             m_social.guildId, m_social.teamId);

    if (mysql_query(db, sql) != 0)
    {
        LOG_ERR("SessionUser::save SQL err: %s", mysql_error(db));
        return false;
    }

    m_dirty = false;
    LOG_DEBUG("SessionUser::save userID=%llu → t_relation", GetID());
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
