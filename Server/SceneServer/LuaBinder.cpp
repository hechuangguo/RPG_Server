/**
 * @file    LuaBinder.cpp
 * @brief  LuaBinder 注册表实现
 */

#include "LuaBinder.h"
#include "SceneEntry.h"

extern "C"
{
#include "lua.h"
#include "lauxlib.h"
}

/** @brief SceneEntry userdata 元表名（与 LuaManager 一致） */
constexpr const char* LUA_SCENE_ENTRY_MT = "SceneEntry";

namespace
{

struct SceneEntryUd
{
    SceneEntry* entry = nullptr;
};

SceneEntryUd* getEntryUd(lua_State* L, int index)
{
    if (!lua_isuserdata(L, index))
        return nullptr;
    if (!lua_getmetatable(L, index))
        return nullptr;
    luaL_getmetatable(L, LUA_SCENE_ENTRY_MT);
    const bool match = lua_rawequal(L, -1, -2);
    lua_pop(L, 2);
    if (!match)
        return nullptr;
    return static_cast<SceneEntryUd*>(lua_touserdata(L, index));
}

} // namespace

LuaBinder::FuncNode* LuaBinder::globalHead = nullptr;
LuaBinder::FuncNode* LuaBinder::entryHead  = nullptr;

void LuaBinder::addGlobal(const char* luaName, LuaCFunction fn)
{
    auto* node  = new FuncNode{luaName, fn, globalHead};
    globalHead  = node;
}

void LuaBinder::addEntryMethod(const char* luaName, LuaCFunction fn)
{
    auto* node = new FuncNode{luaName, fn, entryHead};
    entryHead  = node;
}

SceneEntry* LuaBinder::checkSceneEntry(lua_State* L, int index)
{
    auto* ud = getEntryUd(L, index);
    return ud ? ud->entry : nullptr;
}

void LuaBinder::install(lua_State* L)
{
    for (FuncNode* p = globalHead; p; p = p->next)
        lua_register(L, p->luaName, p->fn);

    luaL_newmetatable(L, LUA_SCENE_ENTRY_MT);
    for (FuncNode* p = entryHead; p; p = p->next)
    {
        lua_pushcfunction(L, p->fn);
        lua_setfield(L, -2, p->luaName);
    }
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);
}
