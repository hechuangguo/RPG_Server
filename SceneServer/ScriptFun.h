/**
 * @file    ScriptFun.h
 * @brief  Lua 调用 C++ 的绑定接口声明（实现在 ScriptFun.cpp）
 *
 * 注册到 Lua 的全局函数：
 * - log_info(msg)
 * - send_to_user(userID, msgID, data)
 * - send_npc_talk_rsp(userID, npcID, step, text, optionsTable)
 *
 * SceneEntry userdata 方法（entry:getXxx）见 registerSceneEntryMetatable。
 */

#pragma once

struct lua_State;
class SceneEntry;

/**
 * @brief Lua → C++ 导出函数集合
 */
class ScriptFun
{
public:
    /** @brief 注册全局函数与 SceneEntry 元表 */
    static void registerAll(lua_State* L);

    /** @brief log_info(msg) */
    static int logInfo(lua_State* L);

    /** @brief send_to_user(userID, msgID, data) */
    static int sendToUser(lua_State* L);

    /**
     * @brief send_npc_talk_rsp(userID, npcID, dialogStep, text, options)
     * @note options 为 Lua 表，元素含 text、next 字段，最多 4 项
     */
    static int sendNpcTalkRsp(lua_State* L);

    /** @brief 从栈上 userdata 取出 SceneEntry*，失败返回 nullptr */
    static SceneEntry* checkSceneEntry(lua_State* L, int index);

    static int entryGetId(lua_State* L);
    static int entryGetType(lua_State* L);
    static int entryGetName(lua_State* L);
    static int entryGetLevel(lua_State* L);
    static int entryGetHp(lua_State* L);
    static int entryGetMaxHp(lua_State* L);
    static int entryGetMapId(lua_State* L);
    static int entryGetPos(lua_State* L);
    static int entrySetHp(lua_State* L);
    static int entrySetPos(lua_State* L);
};
