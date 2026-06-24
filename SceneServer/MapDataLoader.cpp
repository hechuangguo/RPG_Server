/**
 * @file    MapDataLoader.cpp
 * @brief  MapDataLoader 实现
 */

#include "MapDataLoader.h"

#include "../sdk/util/SimpleJsonUtil.h"
#include "../sdk/log/Logger.h"

namespace
{

constexpr const char* MAP_ROOT = "Common/map/";

} // namespace

std::shared_ptr<MapRuntimeData> loadMapData(uint32_t mapId, uint32_t expectedVersion)
{
    const std::string mapDir = std::string(MAP_ROOT) + std::to_string(mapId);
    auto data = std::make_shared<MapRuntimeData>();
    std::string err;
    if (!loadMapRuntimeData(mapDir, mapId, *data, &err))
    {
        LOG_WARN("地图几何数据加载失败 map=%u dir=%s: %s", mapId, mapDir.c_str(), err.c_str());
        return nullptr;
    }

    if (expectedVersion != 0 && data->version != expectedVersion)
    {
        LOG_ERR("地图版本不一致 map=%u meta=%u config=%u", mapId, data->version,
                expectedVersion);
        return nullptr;
    }

    LOG_INFO("地图几何数据已加载: map=%u version=%u aoiGrid=%.0f dir=%s",
             data->mapId, data->version, data->aoiGridSize, mapDir.c_str());
    return data;
}
