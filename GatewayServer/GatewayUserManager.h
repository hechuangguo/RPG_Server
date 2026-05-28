/**
 * @file    GatewayUserManager.h
 * @brief  GatewayServer 客户端会话管理器（connId 索引）
 */

#pragma once
#include "GatewayUser.h"
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

/**
 * @brief Gateway 客户端会话表（connId → GatewayUser）
 */
class GatewayUserManager
{
public:
    std::shared_ptr<GatewayUser> findUser(ConnID connId) const
    {
        auto it = m_users.find(connId);
        return it != m_users.end() ? it->second : nullptr;
    }

    std::shared_ptr<GatewayUser> addUser(ConnID connId)
    {
        auto user = std::make_shared<GatewayUser>(connId);
        m_users[connId] = user; /**< 新连接写入会话表 */
        return user;
    }

    GatewayUser& getUser(ConnID connId)
    {
        return *m_users.at(connId);
    }

    bool removeUser(ConnID connId)
    {
        return m_users.erase(connId) > 0;
    }

    size_t getUserCount() const { return m_users.size(); }

    /** @brief 收集心跳超时的 connId 列表 */
    std::vector<ConnID> collectExpiredConnIds(uint64_t nowMs, uint64_t timeoutMs) const
    {
        std::vector<ConnID> expired; /**< 超时连接输出列表 */
        for (const auto& [connId, user] : m_users)
        {
            if (nowMs - user->getLastHeartbeat() > timeoutMs)
                expired.push_back(connId);
        }
        return expired;
    }

    /** @brief 遍历全部会话并提供可写引用 */
    void forEach(const std::function<void(ConnID, GatewayUser&)>& fn)
    {
        for (auto& [connId, user] : m_users)
            fn(connId, *user);
    }

private:
    std::unordered_map<ConnID, std::shared_ptr<GatewayUser>> m_users; /**< connId -> 会话对象 */
};
