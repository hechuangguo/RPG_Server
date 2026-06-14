/**
 * @file    ZoneInfoStore.cpp
 * @brief   ZoneInfoStore 实现
 */

#include "ZoneInfoStore.h"
#include "ServerListLoader.h"
#include "../sdk/log/Logger.h"

#include <cstdlib>

bool ZoneInfoStore::loadFromFile(const char* path)
{
    std::vector<ZoneInfoRow> loaded;
    std::string err;
    if (!ServerListLoader::Load(path, loaded, &err))
    {
        LOG_ERR("ZoneInfoStore loadFromFile failed: %s", err.c_str());
        return false;
    }
    m_rows = std::move(loaded);
    rebuildEnabledOrder();
    LOG_INFO("ServerList loaded from %s: %zu entries (%zu enabled)",
             path, m_rows.size(), m_enabledIndices.size());
    return true;
}

bool ZoneInfoStore::loadFromDb(MYSQL* db)
{
    if (!db)
        return false;

    const char* sql =
        "SELECT zone_id, game_type, name, ip, super_port, enabled "
        "FROM ZoneInfo ORDER BY game_type, zone_id";
    if (mysql_query(db, sql) != 0)
    {
        LOG_ERR("ZoneInfoStore query failed: %s", mysql_error(db));
        return false;
    }

    MYSQL_RES* res = mysql_store_result(db);
    if (!res)
    {
        LOG_ERR("ZoneInfoStore store_result failed: %s", mysql_error(db));
        return false;
    }

    m_rows.clear();
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)) != nullptr)
    {
        ZoneInfoRow r;
        r.zoneId = row[0] ? static_cast<uint32_t>(strtoul(row[0], nullptr, 10)) : 0;
        r.gameType = row[1] ? static_cast<uint8_t>(strtoul(row[1], nullptr, 10)) : 0;
        r.name = row[2] ? row[2] : "";
        r.ip = row[3] ? row[3] : "";
        r.superPort = row[4] ? static_cast<uint16_t>(strtoul(row[4], nullptr, 10)) : 0;
        r.enabled = row[5] && strtoul(row[5], nullptr, 10) != 0;
        m_rows.push_back(std::move(r));
    }
    mysql_free_result(res);

    rebuildEnabledOrder();
    LOG_INFO("ZoneInfo loaded: %zu entries (%zu enabled)",
             m_rows.size(), m_enabledIndices.size());
    return true;
}

bool ZoneInfoStore::pickRoundRobin(ZoneInfoRow& out)
{
    if (m_enabledIndices.empty())
        return false;
    const size_t idx = m_enabledIndices[m_rrIndex % m_enabledIndices.size()];
    m_rrIndex = (m_rrIndex + 1) % m_enabledIndices.size();
    out = m_rows[idx];
    return true;
}

bool ZoneInfoStore::isZoneEnabled(uint8_t gameType, uint32_t zoneId) const
{
    for (const ZoneInfoRow& r : m_rows)
    {
        if (r.gameType == gameType && r.zoneId == zoneId)
            return r.enabled;
    }
    return false;
}

void ZoneInfoStore::rebuildEnabledOrder()
{
    m_enabledIndices.clear();
    m_enabledIndices.reserve(m_rows.size());
    for (size_t i = 0; i < m_rows.size(); ++i)
    {
        if (m_rows[i].enabled)
            m_enabledIndices.push_back(i);
    }
    if (m_rrIndex >= m_enabledIndices.size())
        m_rrIndex = 0;
}

void ZoneInfoStore::listAll(std::vector<ZoneInfoRow>& out, uint8_t gameTypeFilter) const
{
    out.clear();
    out.reserve(m_rows.size());
    for (const ZoneInfoRow& row : m_rows)
    {
        if (gameTypeFilter != 0xFF && row.gameType != gameTypeFilter)
            continue;
        out.push_back(row);
    }
}
