/**
 * @file    SessionUser.cpp
 * @brief  SessionUser 生命周期与 Relation 表读写
 */

#include "SessionUser.h"
#include "SessionServer.h"
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

void SessionUser::applyRelationRow(const RelationRowData& row)
{
    char guildBuf[24];
    char teamBuf[16];
    snprintf(guildBuf, sizeof(guildBuf), "%llu", static_cast<unsigned long long>(row.guildId));
    snprintf(teamBuf, sizeof(teamBuf), "%u", row.teamId);
    applyRelationFromDbRow(row.friendsJson.c_str(), row.blacklistJson.c_str(),
                           guildBuf, teamBuf,
                           row.binary.empty() ? nullptr
                                              : reinterpret_cast<const char*>(row.binary.data()),
                           static_cast<unsigned long>(row.binary.size()));
}

RelationRowData SessionUser::toRelationRow() const
{
    RelationRowData row;
    row.userID       = GetID();
    row.friendsJson  = joinUserIds(m_social.friends);
    row.blacklistJson = joinUserIds(m_social.blackList);
    row.guildId      = m_social.guildId;
    row.teamId       = m_social.teamId;
    row.binary       = m_social.binary;
    return row;
}

bool SessionUser::load(SessionServer& server)
{
    if (!m_initialized && !init()) return false;
    RelationRowData row;
    if (!server.loadRelationSync(GetID(), row))
        return false;
    applyRelationRow(row);
    LOG_DEBUG("SessionUser::load userID=%llu friends=%zu binary=%zu",
              GetID(), m_social.friends.size(), m_social.binary.size());
    return true;
}

bool SessionUser::save(SessionServer& server)
{
    if (!needSave()) return true;
    if (!server.saveRelation(toRelationRow()))
        return false;
    m_dirty = false;
    LOG_DEBUG("SessionUser::save userID=%llu → Record binary=%zu",
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
