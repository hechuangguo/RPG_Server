/**
 * @file    MapDataLoader.cpp
 * @brief  MapDataLoader 实现
 */

#include "MapDataLoader.h"

#include "../sdk/util/SimpleJsonUtil.h"
#include "../sdk/log/Logger.h"

namespace
{

std::string resolveRuntimeDir(const std::string& mapFile, uint32_t mapId)
{
    if (mapFile.empty())
        return "maps/runtime/" + std::to_string(mapId);

    if (mapFile.find("maps/runtime") != std::string::npos)
        return mapFile;

    if (mapFile.size() > 4 && mapFile.rfind(".map") == mapFile.size() - 4)
    {
        const auto slash = mapFile.find_last_of('/');
        if (slash != std::string::npos)
        {
            const std::string base = mapFile.substr(slash + 1);
            const size_t dot = base.rfind('.');
            const std::string idStr = base.substr(0, dot);
            return "maps/runtime/" + idStr;
        }
        return "maps/runtime/" + std::to_string(mapId);
    }

    return mapFile;
}

} // namespace

std::shared_ptr<MapRuntimeData> loadMapDataFromConfig(const std::string& mapFile,
                                                      uint32_t mapId)
{
    const std::string runtimeDir = resolveRuntimeDir(mapFile, mapId);
    auto data = std::make_shared<MapRuntimeData>();
    std::string err;
    if (!loadMapRuntimeData(runtimeDir, mapId, *data, &err))
    {
        LOG_WARN("地图 runtime 加载失败 map=%u dir=%s: %s", mapId, runtimeDir.c_str(),
                 err.c_str());
        return nullptr;
    }

    LOG_INFO("地图 runtime 已加载: map=%u version=%u aoiGrid=%.0f dir=%s",
             data->mapId, data->version, data->aoiGridSize, runtimeDir.c_str());
    return data;
}
