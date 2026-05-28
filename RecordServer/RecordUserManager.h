/**
 * @file    RecordUserManager.h
 * @brief  RecordServer 用户缓存管理器
 */

#pragma once
#include "RecordUser.h"
#include <functional>
#include <memory>
#include <unordered_map>

/**
 * @brief RecordServer 用户数据缓存（userId → RecordUser）
 */
class RecordUserManager
{
public:
    bool contains(UserID userId) const
    {
        return m_users.find(userId) != m_users.end();
    }

    std::shared_ptr<RecordUser> findUser(UserID userId) const
    {
        auto it = m_users.find(userId);
        return it != m_users.end() ? it->second : nullptr;
    }

    bool addUser(UserID userId, std::shared_ptr<RecordUser> user)
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

    void forEach(const std::function<void(UserID, RecordUser&)>& fn)
    {
        for (auto& [userId, user] : m_users)
            fn(userId, *user);
    }

private:
    std::unordered_map<UserID, std::shared_ptr<RecordUser>> m_users; /**< Record 用户缓存表 */
};
