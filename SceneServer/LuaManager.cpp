/**
 * @file    LuaManager.cpp
 * @brief  LuaManager 实现 —— C++ 调用 Lua 与 SceneEntry 压栈
 */

#include "LuaManager.h"
#include "ScriptFun.h"
#include "../sdk/log/Logger.h"

extern "C"
{
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}

#include <cstdio>

namespace
{

/** @brief userdata 内保存的 SceneEntry 指针（不拥有对象） */
struct SceneEntryUd
{
    SceneEntry* entry = nullptr;
};

} // namespace

LuaArg LuaArg::nil() { return LuaArg{}; }

LuaArg LuaArg::boolean(bool v)
{
    LuaArg a;
    a.type = Type::BOOL;
    a.boolVal = v;
    return a;
}

LuaArg LuaArg::integer(int64_t v)
{
    LuaArg a;
    a.type = Type::INT;
    a.intVal = v;
    return a;
}

LuaArg LuaArg::number(double v)
{
    LuaArg a;
    a.type = Type::NUMBER;
    a.numberVal = v;
    return a;
}

LuaArg LuaArg::string(const std::string& v)
{
    LuaArg a;
    a.type = Type::STRING;
    a.strVal = v;
    return a;
}

LuaArg LuaArg::string(const char* v)
{
    return LuaArg::string(std::string(v ? v : ""));
}

LuaArg LuaArg::binary(const char* data, size_t len)
{
    LuaArg a;
    a.type = Type::BINARY;
    if (data && len > 0)
        a.strVal.assign(data, len);
    return a;
}

LuaManager::~LuaManager()
{
    shutdown();
}

bool LuaManager::init(const char* initScriptPath)
{
    shutdown();

    m_lua = luaL_newstate();
    if (!m_lua)
    {
        LOG_FATAL("LuaManager: luaL_newstate failed");
        return false;
    }

    luaL_openlibs(m_lua);
    ensureSceneEntryMetatable(m_lua);
    ScriptFun::registerAll(m_lua);

    luaL_dostring(m_lua, "package.path = package.path .. ';../script/?.lua'");

    if (luaL_dofile(m_lua, initScriptPath) != LUA_OK)
    {
        LOG_WARN("LuaManager: load %s failed: %s", initScriptPath,
                 lua_tostring(m_lua, -1));
        lua_pop(m_lua, 1);
    }

    LOG_INFO("LuaManager initialized");
    return true;
}

void LuaManager::shutdown()
{
    if (m_lua)
    {
        lua_close(m_lua);
        m_lua = nullptr;
    }
}

void LuaManager::ensureSceneEntryMetatable(lua_State* L)
{
    if (luaL_newmetatable(L, LUA_SCENE_ENTRY_MT))
    {
        lua_pushvalue(L, -1);
        lua_setfield(L, -2, "__index");
        lua_pop(L, 1);
    }
    else
    {
        lua_pop(L, 1);
    }
}

void LuaManager::pushSceneEntry(SceneEntry* entry)
{
    if (!m_lua)
        return;

    if (!entry)
    {
        lua_pushnil(m_lua);
        return;
    }

    auto* ud = static_cast<SceneEntryUd*>(
        lua_newuserdatauv(m_lua, sizeof(SceneEntryUd), 0));
    ud->entry = entry;

    luaL_getmetatable(m_lua, LUA_SCENE_ENTRY_MT);
    lua_setmetatable(m_lua, -2);
}

void LuaManager::pushArg(const LuaArg& arg)
{
    switch (arg.type)
    {
    case LuaArg::Type::NIL:
        lua_pushnil(m_lua);
        break;
    case LuaArg::Type::BOOL:
        lua_pushboolean(m_lua, arg.boolVal ? 1 : 0);
        break;
    case LuaArg::Type::INT:
        lua_pushinteger(m_lua, static_cast<lua_Integer>(arg.intVal));
        break;
    case LuaArg::Type::NUMBER:
        lua_pushnumber(m_lua, arg.numberVal);
        break;
    case LuaArg::Type::STRING:
        lua_pushlstring(m_lua, arg.strVal.c_str(), arg.strVal.size());
        break;
    case LuaArg::Type::BINARY:
        lua_pushlstring(m_lua, arg.strVal.data(), arg.strVal.size());
        break;
    }
}

void LuaManager::pushArgs(std::initializer_list<LuaArg> args)
{
    for (const auto& a : args)
        pushArg(a);
}

bool LuaManager::invokeGlobal(SceneEntry* entry, const char* funcName,
                              std::initializer_list<LuaArg> args,
                              bool withEntry, int nret)
{
    if (!m_lua || !funcName)
        return false;

    lua_getglobal(m_lua, funcName);
    if (!lua_isfunction(m_lua, -1))
    {
        lua_pop(m_lua, 1);
        return false;
    }

    int nargs = 0;
    if (withEntry)
    {
        pushSceneEntry(entry);
        ++nargs;
    }
    pushArgs(args);
    nargs += static_cast<int>(args.size());

    if (lua_pcall(m_lua, nargs, nret, 0) != LUA_OK)
    {
        LOG_WARN("[Lua] call %s failed: %s", funcName, lua_tostring(m_lua, -1));
        lua_pop(m_lua, 1);
        return false;
    }
    return true;
}

bool LuaManager::callScriptBool(SceneEntry* entry, const char* funcName,
                                std::initializer_list<LuaArg> args)
{
    if (!invokeGlobal(entry, funcName, args, true, 1))
        return false;

    bool result = false;
    if (lua_isboolean(m_lua, -1))
        result = lua_toboolean(m_lua, -1) != 0;
    else if (lua_isinteger(m_lua, -1))
        result = lua_tointeger(m_lua, -1) != 0;

    lua_pop(m_lua, 1);
    return result;
}

int64_t LuaManager::callScriptInt(SceneEntry* entry, const char* funcName,
                                  std::initializer_list<LuaArg> args,
                                  int64_t defaultVal)
{
    if (!invokeGlobal(entry, funcName, args, true, 1))
        return defaultVal;

    int64_t result = defaultVal;
    if (lua_isinteger(m_lua, -1))
        result = static_cast<int64_t>(lua_tointeger(m_lua, -1));
    else if (lua_isnumber(m_lua, -1))
        result = static_cast<int64_t>(lua_tonumber(m_lua, -1));

    lua_pop(m_lua, 1);
    return result;
}

std::vector<int64_t> LuaManager::callScriptList(SceneEntry* entry,
                                                const char* funcName,
                                                std::initializer_list<LuaArg> args)
{
    std::vector<int64_t> out;
    if (!invokeGlobal(entry, funcName, args, true, 1))
        return out;

    if (lua_istable(m_lua, -1))
    {
        lua_Integer len = luaL_len(m_lua, -1);
        for (lua_Integer i = 1; i <= len; ++i)
        {
            lua_geti(m_lua, -1, i);
            if (lua_isinteger(m_lua, -1))
                out.push_back(static_cast<int64_t>(lua_tointeger(m_lua, -1)));
            else if (lua_isnumber(m_lua, -1))
                out.push_back(static_cast<int64_t>(lua_tonumber(m_lua, -1)));
            lua_pop(m_lua, 1);
        }
    }
    else if (lua_isinteger(m_lua, -1))
    {
        out.push_back(static_cast<int64_t>(lua_tointeger(m_lua, -1)));
    }

    lua_pop(m_lua, 1);
    return out;
}

bool LuaManager::callGlobalVoid(const char* funcName,
                                std::initializer_list<LuaArg> args)
{
    return invokeGlobal(nullptr, funcName, args, false, 0);
}

bool LuaManager::callGlobalBool(const char* funcName,
                                std::initializer_list<LuaArg> args,
                                bool defaultVal)
{
    if (!invokeGlobal(nullptr, funcName, args, false, 1))
        return defaultVal;

    bool result = defaultVal;
    if (lua_isboolean(m_lua, -1))
        result = lua_toboolean(m_lua, -1) != 0;
    else if (lua_isinteger(m_lua, -1))
        result = lua_tointeger(m_lua, -1) != 0;

    lua_pop(m_lua, 1);
    return result;
}

int64_t LuaManager::callGlobalInt(const char* funcName,
                                  std::initializer_list<LuaArg> args,
                                  int64_t defaultVal)
{
    if (!invokeGlobal(nullptr, funcName, args, false, 1))
        return defaultVal;

    int64_t result = defaultVal;
    if (lua_isinteger(m_lua, -1))
        result = static_cast<int64_t>(lua_tointeger(m_lua, -1));
    else if (lua_isnumber(m_lua, -1))
        result = static_cast<int64_t>(lua_tonumber(m_lua, -1));

    lua_pop(m_lua, 1);
    return result;
}
