/**
 * @file    SessionUserManager.cpp
 * @brief  SessionUserManager 实现与 Relation 表预载
 */

#include "SessionUserManager.h"
#include "../sdk/log/Logger.h"
#include <mysql/mysql.h>
#include <cstdlib>

bool SessionUserManager::init(MYSQL* db)
{
    if (!db)
        return false;

    static const char* kSql =
        "SELECT user_id,friends_json,blacklist_json,guild_id,team_id,`binary` FROM Relation";

    if (mysql_query(db, kSql) != 0)
    {
        LOG_ERR("SessionUserManager::init SQL err: %s", mysql_error(db));
        return false;
    }

    MYSQL_RES* res = mysql_store_result(db);
    if (!res)
    {
        LOG_ERR("SessionUserManager::init store_result err: %s", mysql_error(db));
        return false;
    }

    size_t loaded = 0;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)) != nullptr)
    {
        unsigned long* lengths = mysql_fetch_lengths(res);
        if (!row[0])
            continue;

        const UserID uid = static_cast<UserID>(strtoull(row[0], nullptr, 10));
        auto user = getOrCreateUser(uid);
        const unsigned long binaryLen = lengths ? lengths[5] : 0;
        user->applyRelationFromDbRow(row[1], row[2], row[3], row[4], row[5], binaryLen);
        ++loaded;
    }

    mysql_free_result(res);
    LOG_INFO("SessionUserManager: preloaded %zu Relation row(s), cache size=%zu",
             loaded, m_users.size());
    return true;
}

std::shared_ptr<SessionUser> SessionUserManager::findUser(UserID userId) const
{
    auto it = m_users.find(userId);
    return it != m_users.end() ? it->second : nullptr;
}

std::shared_ptr<SessionUser> SessionUserManager::getOrCreateUser(UserID userId)
{
    auto user = findUser(userId);
    if (user)
        return user;

    UserBase base;
    base.userID = userId;
    user = SessionUser::create(base);
    user->init();
    m_users.emplace(userId, user);
    return user;
}

bool SessionUserManager::removeUser(UserID userId)
{
    return m_users.erase(userId) > 0;
}

size_t SessionUserManager::getUserCount() const
{
    return m_users.size();
}

void SessionUserManager::forEach(
    const std::function<void(UserID, const std::shared_ptr<SessionUser>&)>& fn) const
{
    for (const auto& [userId, user] : m_users)
        fn(userId, user);
}

void SessionUserManager::pushOfflineMsg(UserID toId, uint16_t msgId, const char* data, uint16_t len)
{
    OfflineMsg msg;
    msg.toId  = toId;
    msg.msgId = msgId;
    if (data && len > 0)
        msg.data.assign(data, data + len);
    m_offlineMsgs[toId].push_back(std::move(msg));
}

std::vector<OfflineMsg>& SessionUserManager::offlineMsgs(UserID toId)
{
    return m_offlineMsgs[toId];
}
