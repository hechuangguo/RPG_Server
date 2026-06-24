/**
 * @file    MapConfigLoader.h
 * @brief  从 database/map_config.lua 加载策划地图表（Common/DataDoc/map.xlsx 生成）
 */

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

/** @brief map_config 单行策划数据 */
struct MapTableEntry
{
    uint32_t    mapId = 0;
    std::string name;
    uint32_t    mapType = 0;
    uint32_t    maxPlayer = 200;
    bool        enabled = true;
    uint32_t    version = 0;
    std::string addressableKey;
    float       spawnX = 0.f;
    float       spawnY = 0.f;
    float       spawnZ = 0.f;
};

/**
 * @brief 策划地图表加载器（静态工具类）
 */
class MapConfigLoader
{
public:
    /**
     * @brief 加载 database/map_config.lua
     * @param errOut 可选；失败时写入可读错误信息
     * @return 成功返回 true
     */
    static bool load(std::string* errOut = nullptr);

    /** @brief 按 mapId 查找；未加载或未找到返回 nullptr */
    static const MapTableEntry* find(uint32_t mapId);

    /** @brief 默认新手地图（mapId=1001）策划行；无则 nullptr */
    static const MapTableEntry* getDefaultNewbieMap();

    /** @brief 是否已成功加载至少一条记录 */
    static bool isLoaded() { return !entries().empty(); }

private:
    static std::unordered_map<uint32_t, MapTableEntry>& entries();
};
