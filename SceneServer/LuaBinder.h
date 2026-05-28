/**
 * @file    LuaBinder.h
 * @brief  Lua ↔ C++ 绑定基础设施 —— 静态自动注册，免手写 registerAll
 *
 * 新增 Lua 全局函数（只需在 .cpp 写一处）：
 * @code
 *   LUA_GLOBAL_FUNC("my_func", myFunc)
 *   {
 *       const char* msg = luaL_checkstring(L, 1);
 *       // ...
 *       return 0;
 *   }
 * @endcode
 *
 * 新增 SceneEntry 元方法（entry:myMethod()）：
 * @code
 *   LUA_ENTRY_FUNC("myMethod", entryMyMethod)
 *   {
 *       auto* entry = LuaBinder::checkSceneEntry(L, 1);
 *       // ...
 *       return 0;
 *   }
 * @endcode
 *
 * 启动时调用 LuaBinder::install(L) 一次性注册所有已声明的函数。
 */

#pragma once

struct lua_State;
class SceneEntry;

using LuaCFunction = int (*)(lua_State*);

/**
 * @brief Lua 绑定注册表（全局函数 + SceneEntry 元方法）
 */
class LuaBinder
{
public:
    /** @brief 注册静态收集到的全部绑定（全局 + SceneEntry 元表） */
    static void install(lua_State* L);

    /** @brief 从栈上 userdata 取出 SceneEntry*，失败返回 nullptr */
    static SceneEntry* checkSceneEntry(lua_State* L, int index);

    /** @brief 供宏使用：登记全局 Lua 函数 */
    static void addGlobal(const char* luaName, LuaCFunction fn);

    /** @brief 供宏使用：登记 SceneEntry 元方法 */
    static void addEntryMethod(const char* luaName, LuaCFunction fn);

private:
    struct FuncNode
    {
        const char*  luaName; /**< Lua 侧名称 */
        LuaCFunction fn;      /**< C 函数指针 */
        FuncNode*    next;    /**< 单链表下一个节点 */
    };

    static FuncNode* globalHead; /**< 全局函数注册链表头 */
    static FuncNode* entryHead;  /**< SceneEntry 方法注册链表头 */
};

/**
 * @brief 定义并自动注册 Lua 全局函数
 * @param luaName  Lua 侧函数名（字符串）
 * @param cppFunc  C++ 实现函数名
 */
#define LUA_GLOBAL_FUNC(luaName, cppFunc)                          \
    static int cppFunc(lua_State* L);                              \
    namespace                                                      \
    {                                                              \
        struct LuaAutoGlobal_##cppFunc                             \
        {                                                          \
            LuaAutoGlobal_##cppFunc()                              \
            {                                                        \
                LuaBinder::addGlobal(luaName, cppFunc);             \
            }                                                        \
        };                                                         \
        const LuaAutoGlobal_##cppFunc g_luaAutoGlobal_##cppFunc{}; \
    }                                                              \
    static int cppFunc(lua_State* L)

/**
 * @brief 定义并自动注册 SceneEntry userdata 方法
 * @param luaName  Lua 侧方法名
 * @param cppFunc  C++ 实现函数名
 */
#define LUA_ENTRY_FUNC(luaName, cppFunc)                           \
    static int cppFunc(lua_State* L);                              \
    namespace                                                      \
    {                                                              \
        struct LuaAutoEntry_##cppFunc                              \
        {                                                          \
            LuaAutoEntry_##cppFunc()                               \
            {                                                        \
                LuaBinder::addEntryMethod(luaName, cppFunc);       \
            }                                                        \
        };                                                         \
        const LuaAutoEntry_##cppFunc g_luaAutoEntry_##cppFunc{};   \
    }                                                              \
    static int cppFunc(lua_State* L)
