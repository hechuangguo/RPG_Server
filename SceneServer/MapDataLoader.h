/**
 * @file    MapDataLoader.h
 * @brief  加载 Common/map/{mapId}/ 地图几何数据
 */

#pragma once

#include "../sdk/util/MapRuntimeTypes.h"

#include <memory>
#include <cstdint>

/**
 * @brief 按 mapId 加载 Common/map/{mapId} 目录
 * @param mapId 地图模板 ID
 * @param expectedVersion 策划表 version；0 表示跳过版本校验
 * @return 成功返回 MapRuntimeData；失败返回 nullptr
 */
std::shared_ptr<MapRuntimeData> loadMapData(uint32_t mapId, uint32_t expectedVersion = 0);
