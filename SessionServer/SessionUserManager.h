/**
 * @file    SessionUserManager.h
 * @brief  SessionServer 用户管理器 —— 在线用户缓存与离线消息队列
 */

#pragma once
#include "SessionUser.h"
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

/**
 * @brief 离线消息（目标用户不在线时暂存）
 */
struct OfflineMsg
{
    UserID              toId;    /**< 目标用户 ID */
    uint16_t            msgId;   /**< 协议号 */
    std::vector<char>   data;    /**< 消息体 */
};

/**
 * @brief SessionServer 用户集合管理
 */
class SessionUserManager
{
public:
    std::shared_ptr<SessionUser> findUser(UserID userId) const
    {
        auto it = m_users.find(userId);
        return it != m_users.end() ? it->second : nullptr;
    }

    std::shared_ptr<SessionUser> getOrCreateUser(UserID userId)
    {
        auto user = findUser(userId);
        if (user) return user;

        UserBase base;
        base.userID = userId;  /**< 新建最小用户基线，仅补 userId */
        user = SessionUser::create(base);
        user->init();
        m_users.emplace(userId, user);
        return user;
    }

    bool removeUser(UserID userId)
    {
        return m_users.erase(userId) > 0;
    }

    size_t getUserCount() const { return m_users.size(); }

    void forEach(const std::function<void(UserID, const std::shared_ptr<SessionUser>&)>& fn) const
    {
        for (const auto& [userId, user] : m_users)
            fn(userId, user);
    }

    void pushOfflineMsg(UserID toId, uint16_t msgId, const char* data, uint16_t len)
    {
        OfflineMsg msg;
        msg.toId  = toId;
        msg.msgId = msgId;
        if (data && len > 0)
            msg.data.assign(data, data + len);
        m_offlineMsgs[toId].push_back(std::move(msg));
    }

    std::vector<OfflineMsg>& offlineMsgs(UserID toId)
    {
        return m_offlineMsgs[toId];
    }

private:
    std::unordered_map<UserID, std::shared_ptr<SessionUser>> m_users;        /**< 在线用户缓存 */
    std::unordered_map<UserID, std::vector<OfflineMsg>>     m_offlineMsgs;   /**< 离线消息队列 */
};
