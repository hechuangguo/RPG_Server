/**
 * @file    SceneUserManager.cpp
 * @brief  SceneUserManager 实现
 */

#include "SceneUserManager.h"

std::shared_ptr<SceneUser> SceneUserManager::findUser(UserID userId) const
{
    auto it = m_users.find(userId);
    return it != m_users.end() ? it->second : nullptr;
}

std::shared_ptr<SceneUser> SceneUserManager::findUserByClientConn(uint32_t clientConnId) const
{
    for (const auto& [userId, user] : m_users)
    {
        (void)userId;
        if (user->getGatewayClientConn() == clientConnId)
            return user;
    }
    return nullptr;
}

bool SceneUserManager::addUser(UserID userId, std::shared_ptr<SceneUser> user)
{
    if (!user)
        return false;
    m_users[userId] = std::move(user);
    return true;
}

bool SceneUserManager::removeUser(UserID userId)
{
    return m_users.erase(userId) > 0;
}

size_t SceneUserManager::getUserCount() const
{
    return m_users.size();
}

void SceneUserManager::forEach(
    const std::function<void(UserID, const std::shared_ptr<SceneUser>&)>& fn) const
{
    for (const auto& [userId, user] : m_users)
        fn(userId, user);
}

void SceneUserManager::forEachMutable(const std::function<void(UserID, SceneUser&)>& fn)
{
    for (auto& [userId, user] : m_users)
        fn(userId, *user);
}
