/**
 * @file    SimpleJsonUtil.h
 * @brief  轻量 JSON 解析（Common/map meta.json / spawns.json）
 */

#pragma once

#include "MapRuntimeTypes.h"

#include <string>

/** @brief 从 Common/map/{mapId}/ 目录加载 MapRuntimeData */
bool loadMapRuntimeData(const std::string& mapDir, uint32_t mapId, MapRuntimeData& out,
                        std::string* errOut = nullptr);
