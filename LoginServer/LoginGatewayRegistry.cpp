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

bool LoginGatewayRegistry::pickByZone(uint32_t zoneId, uint8_t gameType, LoginGatewayEntry& out)
{
    std::vector<uint32_t> zoneIds;
    zoneIds.reserve(m_entries.size());
    for (const auto& kv : m_entries)
    {
        if (kv.second.zoneId == zoneId && kv.second.gameType == gameType)
            zoneIds.push_back(kv.first);
    }
    if (zoneIds.empty())
        return false;

    const uint64_t key = (static_cast<uint64_t>(gameType) << 32) | zoneId;
    size_t& rr = m_zoneRrIndex[key];
    const uint32_t id = zoneIds[rr % zoneIds.size()];
    rr = (rr + 1) % zoneIds.size();
    return pickByServerId(id, out);
}

size_t LoginGatewayRegistry::countForZone(uint32_t zoneId, uint8_t gameType) const
{
    size_t count = 0;
    for (const auto& kv : m_entries)
    {
        if (kv.second.zoneId == zoneId && kv.second.gameType == gameType)
            ++count;
    }
    return count;
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
