/**
 * @file    ScriptFun.h
 * @brief  Lua → C++ 绑定入口（实现在 ScriptFun.cpp）
 *
 * 新增绑定请编辑 ScriptFun.cpp，使用 LuaBinder.h 中的宏：
 * - LUA_GLOBAL_FUNC  全局函数
 * - LUA_ENTRY_FUNC   SceneEntry 元方法
 *
 * 无需再修改本头文件或手写 registerAll 列表。
 */

#pragma once

struct lua_State;

class ScriptFun
{
public:
    /** @brief 安装 ScriptFun.cpp 中通过宏自动收集的全部绑定 */
    static void registerAll(lua_State* L);
};
