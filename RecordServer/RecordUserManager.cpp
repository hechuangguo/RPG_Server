/**
 * @file    RecordUserManager.cpp
 * @brief  RecordUserManager 实现
 */

#include "RecordUserManager.h"

bool RecordUserManager::contains(UserID userId) const
{
    return m_users.find(userId) != m_users.end();
}

std::shared_ptr<RecordUser> RecordUserManager::findUser(UserID userId) const
{
    auto it = m_users.find(userId);
    return it != m_users.end() ? it->second : nullptr;
}

bool RecordUserManager::addUser(UserID userId, std::shared_ptr<RecordUser> user)
{
    if (!user)
        return false;
    m_users[userId] = std::move(user);
    return true;
}

bool RecordUserManager::removeUser(UserID userId)
{
    return m_users.erase(userId) > 0;
}

size_t RecordUserManager::getUserCount() const
{
    return m_users.size();
}

void RecordUserManager::forEach(const std::function<void(UserID, RecordUser&)>& fn)
{
    for (auto& [userId, user] : m_users)
        fn(userId, *user);
}
