/**
 * @file    ZoneInfoStore.cpp
 * @brief   ZoneInfoStore 实现
 */

#include "ZoneInfoStore.h"
#include "ServerListLoader.h"
#include "../sdk/log/Logger.h"
#include "../sdk/timer/TimerMgr.h"

#include <cstdlib>

namespace
{
constexpr uint64_t ZONE_RUNTIME_STALE_MS = 30000;
constexpr uint64_t ZONE_STARTUP_GRACE_MS = 60000;
} // namespace

uint64_t ZoneInfoStore::zoneKey(uint8_t gameType, uint32_t zoneId)
{
    return (static_cast<uint64_t>(gameType) << 32) | zoneId;
}

bool ZoneInfoStore::loadFromFile(const char* path)
{
    std::vector<ZoneInfoRow> loaded;
    std::string err;
    if (!ServerListLoader::Load(path, loaded, &err))
    {
        LOG_ERR("ZoneInfoStore 从文件加载失败: %s", err.c_str());
        return false;
    }
    m_rows = std::move(loaded);
    rebuildEnabledOrder();
    LOG_INFO("服务器列表加载完成: %s，条目=%zu，可用=%zu",
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
        LOG_ERR("ZoneInfoStore 查询失败: %s", mysql_error(db));
        return false;
    }

    MYSQL_RES* res = mysql_store_result(db);
    if (!res)
    {
        LOG_ERR("ZoneInfoStore store_result 失败: %s", mysql_error(db));
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
    LOG_INFO("ZoneInfo 加载完成: 条目=%zu，可用=%zu",
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

bool ZoneInfoStore::applyZoneReport(const Msg_Login_ZoneStatusReport& report)
{
    ZoneInfoRow row;
    if (!findZone(report.gameType, report.zoneId, row))
        return false;

    ZoneRuntimeRow& rt = m_runtime[zoneKey(report.gameType, report.zoneId)];
    rt.onlineCount = report.onlineCount;
    rt.gatewayCount = report.gatewayCount;
    rt.alive = report.alive != 0;
    rt.lastReportMs = TimerMgr::NowMs();
    return true;
}

bool ZoneInfoStore::findZone(uint8_t gameType, uint32_t zoneId, ZoneInfoRow& out) const
{
    for (const ZoneInfoRow& row : m_rows)
    {
        if (row.gameType == gameType && row.zoneId == zoneId)
        {
            out = row;
            return true;
        }
    }
    return false;
}

bool ZoneInfoStore::getRuntime(uint8_t gameType, uint32_t zoneId, ZoneRuntimeRow& out) const
{
    auto it = m_runtime.find(zoneKey(gameType, zoneId));
    if (it == m_runtime.end())
        return false;
    out = it->second;
    return true;
}

uint8_t ZoneInfoStore::computeLoadLevel(const ZoneInfoRow& row,
                                        const ZoneRuntimeRow* runtime,
                                        size_t gatewayCount)
{
    static const uint64_t processStartMs = TimerMgr::NowMs();
    if (!row.enabled)
        return static_cast<uint8_t>(ZoneLoadLevel::MAINTENANCE);

    const uint64_t nowMs = TimerMgr::NowMs();
    const bool withinStartupGrace =
        nowMs >= processStartMs && (nowMs - processStartMs <= ZONE_STARTUP_GRACE_MS);

    if (!runtime || runtime->lastReportMs == 0 ||
        nowMs < runtime->lastReportMs ||
        nowMs - runtime->lastReportMs > ZONE_RUNTIME_STALE_MS)
    {
        if (withinStartupGrace)
            return static_cast<uint8_t>(ZoneLoadLevel::SMOOTH);
        return static_cast<uint8_t>(ZoneLoadLevel::MAINTENANCE);
    }

    if (!runtime->alive || gatewayCount == 0)
    {
        if (withinStartupGrace && gatewayCount > 0)
            return static_cast<uint8_t>(ZoneLoadLevel::SMOOTH);
        return static_cast<uint8_t>(ZoneLoadLevel::MAINTENANCE);
    }

    const uint32_t cap = row.maxOnline > 0 ? row.maxOnline : 1;
    const uint64_t pct = (static_cast<uint64_t>(runtime->onlineCount) * 100) / cap;
    if (pct >= 80)
        return static_cast<uint8_t>(ZoneLoadLevel::FULL);
    if (pct >= 50)
        return static_cast<uint8_t>(ZoneLoadLevel::BUSY);
    return static_cast<uint8_t>(ZoneLoadLevel::SMOOTH);
}

void ZoneInfoStore::fillGatewayLoadFields(const ZoneInfoRow& row,
                                          const ZoneInfoStore& store,
                                          size_t registryGatewayCount,
                                          uint32_t& outOnline,
                                          uint8_t& outGatewayCount,
                                          uint8_t& outLoadLevel)
{
    ZoneRuntimeRow runtime;
    const ZoneRuntimeRow* runtimePtr = nullptr;
    if (store.getRuntime(row.gameType, row.zoneId, runtime))
        runtimePtr = &runtime;

    const size_t runtimeGatewayCount = runtimePtr ? runtimePtr->gatewayCount : 0;
    const size_t effectiveGatewayCount =
        registryGatewayCount > runtimeGatewayCount ? registryGatewayCount : runtimeGatewayCount;
    outOnline = runtimePtr ? runtimePtr->onlineCount : 0;
    outGatewayCount = static_cast<uint8_t>(
        effectiveGatewayCount > 255 ? 255 : effectiveGatewayCount);
    outLoadLevel = computeLoadLevel(row, runtimePtr, effectiveGatewayCount);
}
