/**
 * @file    SceneUserManager.h
 * @brief  SceneServer 在线用户管理器
 */

#pragma once
#include "SceneUser.h"
#include <functional>
#include <memory>
#include <unordered_map>

/**
 * @brief SceneServer 在线用户集合（userId → SceneUser）
 */
class SceneUserManager
{
public:
    std::shared_ptr<SceneUser> findUser(UserID userId) const
    {
        auto it = m_users.find(userId);
        return it != m_users.end() ? it->second : nullptr;
    }

    std::shared_ptr<SceneUser> findUserByClientConn(uint32_t clientConnId) const
    {
        for (const auto& [userId, user] : m_users)
        {
            (void)userId;
            if (user->getGatewayClientConn() == clientConnId)
                return user;
        }
        return nullptr;
    }

    bool addUser(UserID userId, std::shared_ptr<SceneUser> user)
    {
        if (!user) return false;
        m_users[userId] = std::move(user);
        return true;
    }

    bool removeUser(UserID userId)
    {
        return m_users.erase(userId) > 0;
    }

    size_t getUserCount() const { return m_users.size(); }

    void forEach(const std::function<void(UserID, const std::shared_ptr<SceneUser>&)>& fn) const
    {
        for (const auto& [userId, user] : m_users)
            fn(userId, user);
    }

    void forEachMutable(const std::function<void(UserID, SceneUser&)>& fn)
    {
        for (auto& [userId, user] : m_users)
            fn(userId, *user);
    }

private:
    std::unordered_map<UserID, std::shared_ptr<SceneUser>> m_users;
};
