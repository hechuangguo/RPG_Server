/**
 * @file    SessionUserManager.cpp
 * @brief  SessionUserManager 实现与 Relation 预载应用
 */

#include "SessionUserManager.h"
#include "../sdk/log/Logger.h"
#include "../sdk/util/UserBase.h"

void SessionUserManager::applyPreloadRows(const std::vector<RelationRowData>& rows)
{
    for (const RelationRowData& row : rows)
    {
        UserBase base;
        base.userID = row.userID;
        auto user = SessionUser::create(base);
        user->init();
        user->applyRelationRow(row);
        user->markRelationLoaded();
        m_users[row.userID] = user;
    }
    LOG_INFO("会话用户管理器: 预加载关系数据 %zu 行，缓存大小=%zu",
             rows.size(), m_users.size());
}

std::shared_ptr<SessionUser> SessionUserManager::findUser(UserID userId) const
{
    auto it = m_users.find(userId);
    return it != m_users.end() ? it->second : nullptr;
}

std::shared_ptr<SessionUser> SessionUserManager::getOrCreateUser(UserID userId)
{
    auto user = findUser(userId);
    if (user) return user;

    UserBase base;
    base.userID = userId;
    user = SessionUser::create(base);
    user->init();
    m_users[userId] = user;
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
    for (const auto& kv : m_users)
        fn(kv.first, kv.second);
}
