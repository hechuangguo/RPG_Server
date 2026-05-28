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
#include "../sdk/util/Singleton.h"
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
    /** @brief LuaArg 支持的参数类型标签 */
    enum class Type : uint8_t
    {
        NIL = 0, /**< nil */
        BOOL,    /**< bool */
        INT,     /**< int64 */
        NUMBER,  /**< double */
        STRING,  /**< UTF-8 字符串 */
        BINARY,  /**< 二进制块（按长度传递） */
    };
    Type type = Type::NIL;   /**< 参数类型 */
    bool boolVal = false;    /**< 布尔值缓存 */
    int64_t intVal = 0;      /**< 整型值缓存 */
    double numberVal = 0.0;  /**< 浮点值缓存 */
    std::string strVal;      /**< 字符串/二进制缓存 */
    /** @brief 构造 nil 参数 */
    static LuaArg nil();

    /** @brief 构造布尔参数 */
    static LuaArg boolean(bool v);

    /** @brief 构造整型参数 */
    static LuaArg integer(int64_t v);

    /** @brief 构造浮点参数 */
    static LuaArg number(double v);

    /** @brief 构造字符串参数 */
    static LuaArg string(const std::string& v);

    /** @brief 构造 C 字符串参数 */
    static LuaArg string(const char* v);

    /** @brief 构造二进制参数（拷贝到 strVal） */
    static LuaArg binary(const char* data, size_t len);
};

/**
 * @brief SceneServer 侧 Lua 管理器（单例）
 */
class LuaManager : public LazySingleton<LuaManager>
{
public:
    friend class LazySingleton<LuaManager>;

    /** @brief 获取全局唯一实例 */
    static LuaManager& Instance() { return LazySingleton<LuaManager>::Instance(); }

    /** @brief 析构时自动关闭 Lua 虚拟机 */
    ~LuaManager();

    /**
     * @brief 初始化虚拟机并加载入口脚本
     * @param initScriptPath init.lua 路径（相对进程 cwd）
     */
    bool init(const char* initScriptPath = "script/scene/init.lua");

    /** @brief 关闭虚拟机 */
    void shutdown();

    /** @brief Lua 虚拟机是否已就绪 */
    bool isReady() const { return m_lua != nullptr; }

    /** @brief 底层 lua_State 指针（仅供 ScriptFun 等绑定使用） */
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

    /** @brief 调用全局函数并返回 bool */
    bool callGlobalBool(const char* funcName,
                        std::initializer_list<LuaArg> args = {},
                        bool defaultVal = false);

    /** @brief 调用全局函数并返回 int64 */
    int64_t callGlobalInt(const char* funcName,
                          std::initializer_list<LuaArg> args = {},
                          int64_t defaultVal = 0);

private:
    LuaManager() = default;

    /** @brief 将单个 LuaArg 压入 Lua 栈 */
    void pushArg(const LuaArg& arg);

    /** @brief 顺序压入多个 LuaArg */
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

    /** @brief 确保 SceneEntry 元表已注册到 lua_State */
    static void ensureSceneEntryMetatable(lua_State* L);
    lua_State* m_lua = nullptr; /**< Lua 虚拟机句柄 */
};

/** @brief SceneEntry userdata 在 Lua 中的元表名 */
constexpr const char* LUA_SCENE_ENTRY_MT = "SceneEntry";
