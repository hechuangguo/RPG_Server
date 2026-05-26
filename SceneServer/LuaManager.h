/**
 * @file    LuaManager.h
 * @brief  SceneServer Lua 虚拟机封装 —— C++ 调用脚本与参数压栈
 *
 * 职责：
 * - 创建/销毁 lua_State，加载 scene/init.lua
 * - 将 SceneEntry 实例以 userdata 传入 Lua（配合 ScriptFun 元表）
 * - callScriptBool / callScriptInt / callScriptList：带 entry 的脚本回调
 * - callGlobalVoid 等：无 entry 的全局回调（OnTick、OnUserEnter 等）
 */

#pragma once

#include "SceneEntry.h"
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

struct lua_State;

/**
 * @brief 压入 Lua 栈的单个参数
 *
 * 支持整型、浮点、布尔、字符串、二进制块；多参数用 initializer_list 传入。
 */
struct LuaArg
{
    enum class Type : uint8_t
    {
        NIL = 0,
        BOOL,
        INT,
        NUMBER,
        STRING,
        BINARY,
    };

    Type type = Type::NIL;
    bool boolVal = false;
    int64_t intVal = 0;
    double numberVal = 0.0;
    std::string strVal;

    static LuaArg nil();
    static LuaArg boolean(bool v);
    static LuaArg integer(int64_t v);
    static LuaArg number(double v);
    static LuaArg string(const std::string& v);
    static LuaArg string(const char* v);
    static LuaArg binary(const char* data, size_t len);
};

/**
 * @brief SceneServer 侧 Lua 管理器
 */
class LuaManager
{
public:
    LuaManager() = default;
    ~LuaManager();

    LuaManager(const LuaManager&) = delete;
    LuaManager& operator=(const LuaManager&) = delete;

    /**
     * @brief 初始化虚拟机并加载入口脚本
     * @param initScriptPath init.lua 路径（相对进程 cwd）
     */
    bool init(const char* initScriptPath = "script/scene/init.lua");

    /** @brief 关闭虚拟机 */
    void shutdown();

    bool isReady() const { return m_lua != nullptr; }
    lua_State* getState() const { return m_lua; }

    /**
     * @brief 将 SceneEntry 压栈（userdata + SceneEntry 元表）
     * @note entry 生命周期由 SceneServer 管理，脚本回调期间须保持有效
     */
    void pushSceneEntry(SceneEntry* entry);

    /**
     * @brief 调用 Lua 全局函数，首参为 entry（可为 nullptr 则不入栈 entry）
     * @param funcName 全局函数名
     * @param args     除 entry 外的参数列表（0 个或多个）
     */
    bool callScriptBool(SceneEntry* entry, const char* funcName,
                        std::initializer_list<LuaArg> args = {});

    int64_t callScriptInt(SceneEntry* entry, const char* funcName,
                           std::initializer_list<LuaArg> args = {},
                           int64_t defaultVal = 0);

    /**
     * @brief 调用脚本并解析返回值为整数列表（Lua table 顺序遍历，仅收集整型元素）
     */
    std::vector<int64_t> callScriptList(SceneEntry* entry, const char* funcName,
                                        std::initializer_list<LuaArg> args = {});

    /** @brief 无 entry 的全局调用，无返回值 */
    bool callGlobalVoid(const char* funcName,
                        std::initializer_list<LuaArg> args = {});

    bool callGlobalBool(const char* funcName,
                        std::initializer_list<LuaArg> args = {},
                        bool defaultVal = false);

    int64_t callGlobalInt(const char* funcName,
                          std::initializer_list<LuaArg> args = {},
                          int64_t defaultVal = 0);

private:
    void pushArg(const LuaArg& arg);
    void pushArgs(std::initializer_list<LuaArg> args);

    /**
     * @brief 执行全局函数
     * @param withEntry  true 时第一个参数为 SceneEntry userdata
     * @param nret       期望返回值个数
     * @return pcall 是否成功
     */
    bool invokeGlobal(SceneEntry* entry, const char* funcName,
                      std::initializer_list<LuaArg> args,
                      bool withEntry, int nret);

    static void ensureSceneEntryMetatable(lua_State* L);

    lua_State* m_lua = nullptr;
};

/** @brief SceneEntry userdata 在 Lua 中的元表名 */
constexpr const char* LUA_SCENE_ENTRY_MT = "SceneEntry";
