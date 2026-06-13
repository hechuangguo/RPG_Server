/**
 * @file    GatewayUserManager.cpp
 * @brief  GatewayUserManager 实现
 */

#include "GatewayUserManager.h"

std::shared_ptr<GatewayUser> GatewayUserManager::findUser(ConnID connId) const
{
    auto it = m_users.find(connId);
    return it != m_users.end() ? it->second : nullptr;
}

std::shared_ptr<GatewayUser> GatewayUserManager::addUser(ConnID connId)
{
    auto user = std::make_shared<GatewayUser>(connId);
    m_users[connId] = user;
    return user;
}

GatewayUser& GatewayUserManager::getUser(ConnID connId)
{
    return *m_users.at(connId);
}

bool GatewayUserManager::removeUser(ConnID connId)
{
    return m_users.erase(connId) > 0;
}

size_t GatewayUserManager::getUserCount() const
{
    return m_users.size();
}

std::vector<ConnID> GatewayUserManager::collectExpiredConnIds(uint64_t nowMs,
                                                              uint64_t timeoutMs) const
{
    std::vector<ConnID> expired;
    for (const auto& [connId, user] : m_users)
    {
        if (nowMs - user->getLastHeartbeat() > timeoutMs)
            expired.push_back(connId);
    }
    return expired;
}

void GatewayUserManager::forEach(const std::function<void(ConnID, GatewayUser&)>& fn)
{
    for (auto& [connId, user] : m_users)
        fn(connId, *user);
}
