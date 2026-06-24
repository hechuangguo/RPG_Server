/**
 * @file    MapRuntimeTypes.h
 * @brief  Common/map/{mapId}/ JSON 解析后的地图运行时数据结构
 *
 * SceneServer MapDataLoader 解析产物；供 MoveValidator 与 AOI 注册使用。
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

/** @brief 世界 AABB 边界（Y-up） */
struct MapWorldBounds
{
    float minX = 0.f;
    float minY = 0.f;
    float minZ = 0.f;
    float maxX = 0.f;
    float maxY = 0.f;
    float maxZ = 0.f;
};

/** @brief 单个出生/复活点 */
struct MapSpawnPoint
{
    std::string name;
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
};

/** @brief Common/map/{mapId}/ 加载后的内存模型 */
struct MapRuntimeData
{
    uint32_t mapId = 0;
    uint32_t version = 0;
    std::string coordSystem = "Y-up";
    MapWorldBounds bounds;
    float aoiGridSize = 0.f;   /**< 0 = 用 AOIServer 全局默认 */
    float maxWalkSpeed = 4.f;
    float maxRunSpeed = 8.f;
    float maxStepWalk = 6.f;
    float maxStepRun = 12.f;
    MapSpawnPoint defaultSpawn;
    std::vector<MapSpawnPoint> spawns;
    bool hasNavMesh = false;
    std::string runtimeRoot;   /**< 加载目录路径 */
};
