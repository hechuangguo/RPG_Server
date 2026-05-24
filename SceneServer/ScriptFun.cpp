/**
 * @file    ScriptFun.cpp
 * @brief  Lua → C++ 绑定实现
 */

#include "ScriptFun.h"
#include "LuaManager.h"
#include "SceneServer.h"
#include "SceneEntry.h"
#include "../common/ClientMsg.h"
#include "../sdk/log/Logger.h"

constexpr int MAX_NPC_TALK_OPTIONS = 4;

extern "C"
{
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}

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
    bool match = lua_rawequal(L, -1, -2);
    lua_pop(L, 2);
    if (!match)
        return nullptr;
    return static_cast<SceneEntryUd*>(lua_touserdata(L, index));
}

} // namespace

void ScriptFun::registerAll(lua_State* L)
{
    lua_register(L, "log_info", logInfo);
    lua_register(L, "send_to_user", sendToUser);
    lua_register(L, "send_npc_talk_rsp", sendNpcTalkRsp);

    if (luaL_newmetatable(L, LUA_SCENE_ENTRY_MT))
    {
        static const luaL_Reg entryMethods[] = {
            {"getEntryId",   entryGetId},
            {"getEntryType", entryGetType},
            {"getName",      entryGetName},
            {"getLevel",     entryGetLevel},
            {"getHp",        entryGetHp},
            {"getMaxHp",     entryGetMaxHp},
            {"getMapId",     entryGetMapId},
            {"getPos",       entryGetPos},
            {"setHp",        entrySetHp},
            {"setPos",       entrySetPos},
            {nullptr, nullptr},
        };
        luaL_setfuncs(L, entryMethods, 0);
        lua_pushvalue(L, -1);
        lua_setfield(L, -2, "__index");
    }
    lua_pop(L, 1);
}

SceneEntry* ScriptFun::checkSceneEntry(lua_State* L, int index)
{
    auto* ud = getEntryUd(L, index);
    return ud ? ud->entry : nullptr;
}

int ScriptFun::logInfo(lua_State* L)
{
    const char* msg = luaL_checkstring(L, 1);
    LOG_INFO("[Lua] %s", msg);
    return 0;
}

int ScriptFun::sendToUser(lua_State* L)
{
    UserID userId = static_cast<UserID>(luaL_checkinteger(L, 1));
    uint16_t msgId = static_cast<uint16_t>(luaL_checkinteger(L, 2));
    size_t len = 0;
    const char* data = luaL_optlstring(L, 3, "", &len);

    auto* server = SceneServer::Instance();
    if (!server)
        return 0;

    auto user = server->findUser(userId);
    if (!user || user->getGatewayClientConn() == 0)
        return 0;

    server->sendToClient(user->getGatewayClientConn(), msgId, data,
                         static_cast<uint16_t>(len));
    return 0;
}

int ScriptFun::sendNpcTalkRsp(lua_State* L)
{
    UserID userId = static_cast<UserID>(luaL_checkinteger(L, 1));
    EntryID npcId = static_cast<EntryID>(luaL_checkinteger(L, 2));
    int32_t dialogStep = static_cast<int32_t>(luaL_checkinteger(L, 3));
    const char* text = luaL_checkstring(L, 4);

    Msg_S2C_NpcTalkRsp rsp{};
    rsp.code = 0;
    rsp.npcId = npcId;
    rsp.dialogStep = dialogStep;
    snprintf(rsp.text, sizeof(rsp.text), "%s", text);
    rsp.optionCount = 0;

    if (lua_istable(L, 5))
    {
        const lua_Integer n = luaL_len(L, 5);
        for (lua_Integer i = 1; i <= n && rsp.optionCount < MAX_NPC_TALK_OPTIONS; ++i)
        {
            lua_rawgeti(L, 5, i);
            if (!lua_istable(L, -1))
            {
                lua_pop(L, 1);
                continue;
            }

            auto& opt = rsp.options[rsp.optionCount];
            lua_getfield(L, -1, "text");
            if (lua_isstring(L, -1))
                snprintf(opt.text, sizeof(opt.text), "%s", lua_tostring(L, -1));
            lua_pop(L, 1);

            lua_getfield(L, -1, "next");
            opt.nextStep = static_cast<int32_t>(luaL_optinteger(L, -1, -1));
            lua_pop(L, 1);

            lua_pop(L, 1);
            ++rsp.optionCount;
        }
    }

    auto* server = SceneServer::Instance();
    if (!server)
        return 0;

    auto user = server->findUser(userId);
    if (!user || user->getGatewayClientConn() == 0)
        return 0;

    server->sendToClient(user->getGatewayClientConn(),
                         static_cast<uint16_t>(ClientMsgID::S2C_NPC_TALK_RSP),
                         reinterpret_cast<char*>(&rsp), sizeof(rsp));
    return 0;
}

int ScriptFun::entryGetId(lua_State* L)
{
    auto* entry = checkSceneEntry(L, 1);
    if (!entry)
        return luaL_error(L, "invalid SceneEntry");
    lua_pushinteger(L, static_cast<lua_Integer>(entry->getEntryId()));
    return 1;
}

int ScriptFun::entryGetType(lua_State* L)
{
    auto* entry = checkSceneEntry(L, 1);
    if (!entry)
        return luaL_error(L, "invalid SceneEntry");
    lua_pushinteger(L, static_cast<lua_Integer>(entry->getEntryType()));
    return 1;
}

int ScriptFun::entryGetName(lua_State* L)
{
    auto* entry = checkSceneEntry(L, 1);
    if (!entry)
        return luaL_error(L, "invalid SceneEntry");
    lua_pushstring(L, entry->getName().c_str());
    return 1;
}

int ScriptFun::entryGetLevel(lua_State* L)
{
    auto* entry = checkSceneEntry(L, 1);
    if (!entry)
        return luaL_error(L, "invalid SceneEntry");
    lua_pushinteger(L, static_cast<lua_Integer>(entry->getLevel()));
    return 1;
}

int ScriptFun::entryGetHp(lua_State* L)
{
    auto* entry = checkSceneEntry(L, 1);
    if (!entry)
        return luaL_error(L, "invalid SceneEntry");
    lua_pushinteger(L, static_cast<lua_Integer>(entry->getHp()));
    return 1;
}

int ScriptFun::entryGetMaxHp(lua_State* L)
{
    auto* entry = checkSceneEntry(L, 1);
    if (!entry)
        return luaL_error(L, "invalid SceneEntry");
    lua_pushinteger(L, static_cast<lua_Integer>(entry->getMaxHp()));
    return 1;
}

int ScriptFun::entryGetMapId(lua_State* L)
{
    auto* entry = checkSceneEntry(L, 1);
    if (!entry)
        return luaL_error(L, "invalid SceneEntry");
    lua_pushinteger(L, static_cast<lua_Integer>(entry->getMapId()));
    return 1;
}

int ScriptFun::entryGetPos(lua_State* L)
{
    auto* entry = checkSceneEntry(L, 1);
    if (!entry)
        return luaL_error(L, "invalid SceneEntry");
    lua_pushnumber(L, entry->getPosX());
    lua_pushnumber(L, entry->getPosY());
    lua_pushnumber(L, entry->getPosZ());
    return 3;
}

int ScriptFun::entrySetHp(lua_State* L)
{
    auto* entry = checkSceneEntry(L, 1);
    if (!entry)
        return luaL_error(L, "invalid SceneEntry");
    uint32_t hp = static_cast<uint32_t>(luaL_checkinteger(L, 2));
    entry->setHp(hp);
    return 0;
}

int ScriptFun::entrySetPos(lua_State* L)
{
    auto* entry = checkSceneEntry(L, 1);
    if (!entry)
        return luaL_error(L, "invalid SceneEntry");
    float x = static_cast<float>(luaL_checknumber(L, 2));
    float y = static_cast<float>(luaL_checknumber(L, 3));
    float z = static_cast<float>(luaL_checknumber(L, 4));
    entry->setPos(x, y, z);
    return 0;
}
