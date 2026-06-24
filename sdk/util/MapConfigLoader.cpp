/**
 * @file    MapConfigLoader.cpp
 * @brief  MapConfigLoader 实现
 */

#include "MapConfigLoader.h"

#include "../log/Logger.h"

extern "C"
{
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}

#include <cmath>

namespace
{

void setupPackagePath(lua_State* L)
{
    luaL_dostring(L,
                  "package.path = package.path"
                  " .. ';database/?.lua;../database/?.lua'");
}

bool readBoolField(lua_State* L, int tableIndex, const char* key, bool defaultVal)
{
    lua_getfield(L, tableIndex, key);
    if (lua_isboolean(L, -1))
    {
        const bool val = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
        return val;
    }
    if (lua_isnumber(L, -1))
    {
        const bool val = lua_tointeger(L, -1) != 0;
        lua_pop(L, 1);
        return val;
    }
    lua_pop(L, 1);
    return defaultVal;
}

uint32_t readUIntField(lua_State* L, int tableIndex, const char* key, uint32_t defaultVal)
{
    lua_getfield(L, tableIndex, key);
    if (!lua_isnumber(L, -1))
    {
        lua_pop(L, 1);
        return defaultVal;
    }
    const lua_Integer val = lua_tointeger(L, -1);
    lua_pop(L, 1);
    return val >= 0 ? static_cast<uint32_t>(val) : defaultVal;
}

float readFloatField(lua_State* L, int tableIndex, const char* key, float defaultVal)
{
    lua_getfield(L, tableIndex, key);
    if (!lua_isnumber(L, -1))
    {
        lua_pop(L, 1);
        return defaultVal;
    }
    const double val = lua_tonumber(L, -1);
    lua_pop(L, 1);
    return static_cast<float>(val);
}

std::string readStringField(lua_State* L, int tableIndex, const char* key)
{
    lua_getfield(L, tableIndex, key);
    if (!lua_isstring(L, -1))
    {
        lua_pop(L, 1);
        return {};
    }
    const char* text = lua_tostring(L, -1);
    std::string out = text ? text : "";
    lua_pop(L, 1);
    return out;
}

bool parseRow(lua_State* L, int rowIndex, MapTableEntry& out)
{
    if (!lua_istable(L, rowIndex))
        return false;

    out.mapId = readUIntField(L, rowIndex, "id", 0);
    if (out.mapId == 0)
        return false;

    out.name = readStringField(L, rowIndex, "name");
    out.mapType = readUIntField(L, rowIndex, "mapType", 0);
    out.maxPlayer = readUIntField(L, rowIndex, "maxPlayer", out.maxPlayer);
    out.enabled = readBoolField(L, rowIndex, "enabled", true);
    out.version = readUIntField(L, rowIndex, "version", 0);
    out.addressableKey = readStringField(L, rowIndex, "addressableKey");
    out.spawnX = readFloatField(L, rowIndex, "spawnX", 0.f);
    out.spawnY = readFloatField(L, rowIndex, "spawnY", 0.f);
    out.spawnZ = readFloatField(L, rowIndex, "spawnZ", 0.f);
    return true;
}

} // namespace

std::unordered_map<uint32_t, MapTableEntry>& MapConfigLoader::entries()
{
    static std::unordered_map<uint32_t, MapTableEntry> table;
    return table;
}

bool MapConfigLoader::load(std::string* errOut)
{
    entries().clear();

    lua_State* L = luaL_newstate();
    if (!L)
    {
        if (errOut)
            *errOut = "创建 Lua 状态失败";
        return false;
    }

    luaL_openlibs(L);
    setupPackagePath(L);

    if (luaL_dofile(L, "database/map_config.lua") != LUA_OK &&
        luaL_dofile(L, "../database/map_config.lua") != LUA_OK)
    {
        const char* err = lua_tostring(L, -1);
        if (errOut)
            *errOut = err ? err : "无法加载 database/map_config.lua";
        lua_close(L);
        return false;
    }

    if (!lua_istable(L, -1))
    {
        if (errOut)
            *errOut = "map_config 不是表";
        lua_close(L);
        return false;
    }

    lua_pushnil(L);
    while (lua_next(L, -2) != 0)
    {
        MapTableEntry row;
        if (parseRow(L, -1, row))
            entries()[row.mapId] = row;
        lua_pop(L, 1);
    }

    lua_close(L);

    if (entries().empty())
    {
        if (errOut)
            *errOut = "map_config 为空";
        LOG_ERR("策划地图表加载失败: map_config 无有效记录");
        return false;
    }

    LOG_INFO("策划地图表已加载: count=%zu", entries().size());
    return true;
}

const MapTableEntry* MapConfigLoader::find(uint32_t mapId)
{
    const auto it = entries().find(mapId);
    return it != entries().end() ? &it->second : nullptr;
}

const MapTableEntry* MapConfigLoader::getDefaultNewbieMap()
{
    if (const MapTableEntry* row = find(1001))
        return row;
    return nullptr;
}
