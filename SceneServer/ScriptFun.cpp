/**
 * @file    ScriptFun.cpp
 * @brief  Lua → C++ 绑定实现（LUA_*_FUNC 宏自动注册）
 *
 * 新增接口示例：
 * @code
 *   LUA_GLOBAL_FUNC("foo", foo)
 *   {
 *       // luaL_check* 取参
 *       return 0;
 *   }
 * @endcode
 */

#include "ScriptFun.h"
#include "LuaBinder.h"
#include "SceneServer.h"
#include "SceneUserManager.h"
#include "SceneEntry.h"
#include "../Common/MapDataMsg.h"
#include "../Common/ClientMsgBody.h"
#include "../sdk/log/Logger.h"

extern "C"
{
#include "lua.h"
#include "lauxlib.h"
}

// ── 全局函数 ──────────────────────────────────────────────

LUA_GLOBAL_FUNC("log_info", logInfo)
{
    const char* msg = luaL_checkstring(L, 1);
    LOG_INFO("[脚本] %s", msg);
    return 0;
}

LUA_GLOBAL_FUNC("send_to_user", sendToUser)
{
    UserID userId = static_cast<UserID>(luaL_checkinteger(L, 1));
    uint16_t msgId = static_cast<uint16_t>(luaL_checkinteger(L, 2));
    size_t len = 0;
    const char* data = luaL_optlstring(L, 3, "", &len);

    auto* server = SceneServer::Instance();
    if (!server)
        return 0;

    auto user = SceneUserManager::Instance().findUser(userId);
    if (!user || user->getGatewayClientConn() == 0)
        return 0;

    server->sendToClient(user->getGatewayClientConn(), msgId, data,
                         static_cast<uint16_t>(len));
    return 0;
}

LUA_GLOBAL_FUNC("send_npc_talk_rsp", sendNpcTalkRsp)
{
    UserID userId = static_cast<UserID>(luaL_checkinteger(L, 1));
    EntryID npcId = static_cast<EntryID>(luaL_checkinteger(L, 2));
    int32_t dialogStep = static_cast<int32_t>(luaL_checkinteger(L, 3));
    const char* text = luaL_checkstring(L, 4);

    Msg_S2C_NpcTalkRsp rsp{};
    initClientMsg(rsp);
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

    auto user = SceneUserManager::Instance().findUser(userId);
    if (!user || user->getGatewayClientConn() == 0)
        return 0;

    server->sendToClient(user->getGatewayClientConn(),
                         Msg_S2C_NpcTalkRsp::kModule, Msg_S2C_NpcTalkRsp::kSub,
                         reinterpret_cast<char*>(&rsp), sizeof(rsp));
    return 0;
}

// ── SceneEntry 元方法 ─────────────────────────────────────

LUA_ENTRY_FUNC("getEntryId", entryGetId)
{
    auto* entry = LuaBinder::checkSceneEntry(L, 1);
    if (!entry)
        return luaL_error(L, "invalid SceneEntry");
    lua_pushinteger(L, static_cast<lua_Integer>(entry->getEntryId()));
    return 1;
}

LUA_ENTRY_FUNC("getEntryType", entryGetType)
{
    auto* entry = LuaBinder::checkSceneEntry(L, 1);
    if (!entry)
        return luaL_error(L, "invalid SceneEntry");
    lua_pushinteger(L, static_cast<lua_Integer>(entry->getEntryType()));
    return 1;
}

LUA_ENTRY_FUNC("getName", entryGetName)
{
    auto* entry = LuaBinder::checkSceneEntry(L, 1);
    if (!entry)
        return luaL_error(L, "invalid SceneEntry");
    lua_pushstring(L, entry->getName().c_str());
    return 1;
}

LUA_ENTRY_FUNC("getLevel", entryGetLevel)
{
    auto* entry = LuaBinder::checkSceneEntry(L, 1);
    if (!entry)
        return luaL_error(L, "invalid SceneEntry");
    lua_pushinteger(L, static_cast<lua_Integer>(entry->getLevel()));
    return 1;
}

LUA_ENTRY_FUNC("getHp", entryGetHp)
{
    auto* entry = LuaBinder::checkSceneEntry(L, 1);
    if (!entry)
        return luaL_error(L, "invalid SceneEntry");
    lua_pushinteger(L, static_cast<lua_Integer>(entry->getHp()));
    return 1;
}

LUA_ENTRY_FUNC("getMaxHp", entryGetMaxHp)
{
    auto* entry = LuaBinder::checkSceneEntry(L, 1);
    if (!entry)
        return luaL_error(L, "invalid SceneEntry");
    lua_pushinteger(L, static_cast<lua_Integer>(entry->getMaxHp()));
    return 1;
}

LUA_ENTRY_FUNC("getMapId", entryGetMapId)
{
    auto* entry = LuaBinder::checkSceneEntry(L, 1);
    if (!entry)
        return luaL_error(L, "invalid SceneEntry");
    lua_pushinteger(L, static_cast<lua_Integer>(entry->getMapId()));
    return 1;
}

LUA_ENTRY_FUNC("getPos", entryGetPos)
{
    auto* entry = LuaBinder::checkSceneEntry(L, 1);
    if (!entry)
        return luaL_error(L, "invalid SceneEntry");
    lua_pushnumber(L, entry->getPosX());
    lua_pushnumber(L, entry->getPosY());
    lua_pushnumber(L, entry->getPosZ());
    return 3;
}

LUA_ENTRY_FUNC("setHp", entrySetHp)
{
    auto* entry = LuaBinder::checkSceneEntry(L, 1);
    if (!entry)
        return luaL_error(L, "invalid SceneEntry");
    uint32_t hp = static_cast<uint32_t>(luaL_checkinteger(L, 2));
    entry->setHp(hp);
    return 0;
}

LUA_ENTRY_FUNC("setPos", entrySetPos)
{
    auto* entry = LuaBinder::checkSceneEntry(L, 1);
    if (!entry)
        return luaL_error(L, "invalid SceneEntry");
    float x = static_cast<float>(luaL_checknumber(L, 2));
    float y = static_cast<float>(luaL_checknumber(L, 3));
    float z = static_cast<float>(luaL_checknumber(L, 4));
    entry->setPos(x, y, z);
    return 0;
}

void ScriptFun::registerAll(lua_State* L)
{
    LuaBinder::install(L);
}
