/**
 * @file    MapDataLoader.h
 * @brief  加载 maps/runtime/{mapId}/ 地图 runtime 数据
 */

#pragma once

#include "../sdk/util/MapRuntimeTypes.h"

#include <memory>
#include <string>

/**
 * @brief 解析 maps/runtime 目录；兼容 map/{id}.map → maps/runtime/{id}/
 * @param mapFile server_info.xml Map@file 属性
 * @param mapId 地图模板 ID
 * @return 成功返回 MapRuntimeData；失败返回 nullptr
 */
std::shared_ptr<MapRuntimeData> loadMapDataFromConfig(const std::string& mapFile,
                                                      uint32_t mapId);
