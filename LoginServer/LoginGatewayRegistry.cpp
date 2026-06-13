/**
 * @file    LoginGatewayRegistry.cpp
 * @brief   LoginGatewayRegistry 实现
 */

#include "LoginGatewayRegistry.h"

void LoginGatewayRegistry::upsert(const LoginGatewayEntry& entry)
{
    m_entries[entry.gatewayServerId] = entry;
    rebuildOrder();
}

bool LoginGatewayRegistry::touch(uint32_t gatewayServerId)
{
    auto it = m_entries.find(gatewayServerId);
    if (it == m_entries.end())
        return false;
    it->second.lastHeartbeatMs = TimerMgr::NowMs();
    return true;
}

void LoginGatewayRegistry::pruneStale(uint64_t nowMs, uint64_t timeoutMs)
{
    for (auto it = m_entries.begin(); it != m_entries.end();)
    {
        if (nowMs > it->second.lastHeartbeatMs &&
            nowMs - it->second.lastHeartbeatMs > timeoutMs)
        {
            it = m_entries.erase(it);
        }
        else
        {
            ++it;
        }
    }
    rebuildOrder();
}

bool LoginGatewayRegistry::pickRoundRobin(LoginGatewayEntry& out)
{
    if (m_order.empty())
        return false;
    const uint32_t id = m_order[m_rrIndex % m_order.size()];
    m_rrIndex = (m_rrIndex + 1) % m_order.size();
    auto it = m_entries.find(id);
    if (it == m_entries.end())
        return false;
    out = it->second;
    return true;
}

bool LoginGatewayRegistry::pickByServerId(uint32_t gatewayServerId, LoginGatewayEntry& out)
{
    auto it = m_entries.find(gatewayServerId);
    if (it == m_entries.end())
        return false;
    out = it->second;
    return true;
}

void LoginGatewayRegistry::rebuildOrder()
{
    m_order.clear();
    m_order.reserve(m_entries.size());
    for (const auto& kv : m_entries)
        m_order.push_back(kv.first);
    if (m_rrIndex >= m_order.size())
        m_rrIndex = 0;
}
