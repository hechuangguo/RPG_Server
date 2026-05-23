/**
 * @file    MsgDispatcher.h
 * @brief  消息分发器（MsgID → Handler 哈希映射）
 *
 * 各服务器通过 Register() 注册消息处理函数，OnMessage 事件中调用
 * Dispatch() 即可 O(1) 查表派发。
 *
 * 同时包含 LuaHelper 命名空间，供 SceneServer 简化 Lua C API 调用。
 *
 * 使用方式：
 * @code
 *   auto& d = MsgDispatcher::Instance();
 *   d.Register(0x0001, [](uint32_t c, const char* d, uint16_t l){ ... });
 *   // 在网络回调中
 *   MsgDispatcher::Instance().Dispatch(connID, msgID, data, len);
 * @endcode
 */

#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <functional>
#include <memory>
#include <vector>

/**
 * @brief 消息处理函数签名
 * @param connID 来源连接 ID
 * @param data   消息体指针
 * @param len    消息体长度
 */
using MsgHandler = std::function<void(uint32_t connID, const char* data, uint16_t len)>;

/**
 * @brief 消息分发器（单例）
 *
 * 内部维护 unordered_map<uint16_t, MsgHandler>，
 * Register() 注册 → Dispatch() 查表执行。
 */
class MsgDispatcher
{
public:
    /** @brief 获取全局唯一实例 */
    static MsgDispatcher& Instance()
    {
        static MsgDispatcher s;
        return s;
    }

    /**
     * @brief 注册消息处理函数
     * @param msgID   协议号
     * @param handler 处理函数（lambda / std::bind / 函数指针）
     */
    void Register(uint16_t msgID, MsgHandler handler)
    {
        m_handlers[msgID] = std::move(handler);
    }

    /**
     * @brief 派发消息
     * @param connID 来源连接 ID
     * @param msgID  协议号
     * @param data   消息体
     * @param len    消息体长度
     * @return 找到处理函数并成功执行返回 true，否则 false
     */
    bool Dispatch(uint32_t connID, uint16_t msgID, const char* data, uint16_t len)
    {
        auto it = m_handlers.find(msgID);
        if (it == m_handlers.end()) return false;
        it->second(connID, data, len);
        return true;
    }

private:
    std::unordered_map<uint16_t, MsgHandler> m_handlers; /**< msgID → 处理函数 */
};

// ============================================================
//  Lua 绑定辅助
// ============================================================

/** @brief Lua 状态指针（前置声明，避免全量引入 lua.h） */
struct lua_State;

extern "C"
{
    #include "lua.h"
    #include "lualib.h"
    #include "lauxlib.h"
}

/**
 * @brief Lua C API 辅助函数集合
 *
 * 封装常见的 lua_pcall 调用模式，减少样板代码。
 */
namespace LuaHelper
{
    /**
     * @brief 安全调用 Lua 全局函数
     * @param L        Lua 虚拟机
     * @param funcName 全局函数名
     * @param nargs    已压入栈的参数个数
     * @param nret     期望返回值个数
     * @return 调用成功返回 true；函数不存在或执行出错返回 false
     * @note   调用前需自行将参数压栈，调用后返回值在栈顶
     */
    inline bool CallFunc(lua_State* L, const char* funcName, int nargs, int nret)
    {
        lua_getglobal(L, funcName);
        if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return false; }
        if (lua_pcall(L, nargs, nret, 0) != LUA_OK)
        {
            fprintf(stderr, "[Lua] Error calling %s: %s\n",
                    funcName, lua_tostring(L, -1));
            lua_pop(L, 1);
            return false;
        }
        return true;
    }
}
